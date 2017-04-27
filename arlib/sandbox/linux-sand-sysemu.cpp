#ifdef SANDBOX_INTERNAL

//#define _GNU_SOURCE // default (mandatory) in c++
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <ucontext.h>
#include <errno.h>
#include <sched.h>

#include "syscall.h"
#include <sys/socket.h>

//gcc recognizes various function names and reads attributes (such as extern) from the headers, force it not to
namespace mysand { namespace {

//have to redefine the entire libc, fun
//(there's some copying between this and preload.cpp)
static inline void memset(void* ptr, int value, size_t num)
{
	//compiler probably optimizes this
	uint8_t* ptr_ = (uint8_t*)ptr;
	for (size_t i=0;i<num;i++) ptr_[i] = value;
}
static inline void memcpy(void * dest, const void * src, size_t n)
{
	uint8_t* dest_ = (uint8_t*)dest;
	uint8_t* src_ = (uint8_t*)src;
	for (size_t i=0;i<n;i++) dest_[i] = src_[i];
}
static inline size_t strlen(const char * str)
{
	const char * iter = str;
	while (*iter) iter++;
	return iter-str;
}

static inline int close(int fd)
{
	return syscall1(__NR_close, fd);
}

static inline void exit_group(int status)
{
	syscall1(__NR_exit_group, status);
}

static inline ssize_t write(int fd, const void * buf, size_t count)
{
	return syscall3(__NR_write, fd, (long)buf, count);
}

#define fstat fstat_
static inline int fstat(int fd, struct stat * buf)
{
	return syscall2(__NR_fstat, fd, (long)buf);
}

static inline ssize_t send(int sockfd, const void * buf, size_t len, int flags)
{
	return syscall6(__NR_sendto, sockfd, (long)buf, len, flags, (long)NULL, 0); // no send syscall
}

static inline int dup2(int oldfd, int newfd)
{
	return syscall2(__NR_dup2, oldfd, newfd);
}

#define recvmsg recvmsg_ // gcc claims a few syscalls are ambiguous, the outer (extern) one being at least as good as this one
static inline ssize_t recvmsg(int sockfd, struct msghdr * msg, int flags)
{
	return syscall3(__NR_recvmsg, sockfd, (long)msg, flags);
}


//basically my printf
template<typename T> inline void debug(T x)
{
	static const char digits[] = "0123456789ABCDEF";
	char buf[16];
	size_t y = (size_t)x;
	for (int i=0;i<16;i++)
	{
		buf[15-i] = digits[(y>>(i*4))&15];
	}
	write(1, buf, 16);
}
template<typename T> inline void debugd(T x)
{
	char buf[40];
	char* bufend = buf+40;
	char* bufat = bufend;
	size_t y = (size_t)x;
	while (y)
	{
		*--bufat = '0'+(y%10);
		y /= 10;
	}
	if (bufend==bufat) *--bufat='0';
	write(1, bufat, bufend-bufat);
}
template<> inline void debug(const char * x)
{
	write(1, x, strlen(x));
}


#include "linux-sand-internal.h"
#define FD_PARENT 3

static inline int get_broker_rsp()
{
	//TODO: keep br_send replies around
	broker_rsp rsp;
	int fd;
	ssize_t len = recv_fd(FD_PARENT, &rsp, sizeof(rsp), 0, &fd);
	if (len!=sizeof(rsp)) return -EAGAIN;
	if (fd>=0) return fd;
	else return -rsp.err;
}

static inline int open(const char * pathname, int flags, mode_t mode = 0)
{
	broker_req req = { br_open, { (uint32_t)flags, mode } };
	memcpy(req.path, pathname, sizeof(req.path));
	send(FD_PARENT, &req, sizeof(req), 0);
	return get_broker_rsp();
}

static inline int access(const char * pathname, int flags)
{
	int fd = open(pathname, O_RDONLY);
	if (fd<0) return fd;
	
	struct stat st;
	fstat(fd, &st);
	close(fd);
	
	if (flags == F_OK || flags&R_OK) return 0;
	return -ENOSYS; // TODO
}

static inline int stat_(const char * pathname, struct stat * buf)
{
	int fd = open(pathname, O_RDONLY);
	if (fd<0) return fd;
	
	int ret = fstat(fd, buf);
	close(fd);
	return ret;
}

static inline pid_t clone(unsigned long clone_flags, unsigned long newsp,
                          int* parent_tidptr, int* child_tidptr, unsigned long tls)
{
	if (clone_flags&CLONE_CHILD_SETTID) return -EINVAL;
	
	broker_req req = { br_fork };
	send(FD_PARENT, &req, sizeof(req), 0);
	int fd = get_broker_rsp();
	
	pid_t ret = syscall5(__NR_clone, clone_flags|CLONE_CHILD_SETTID, newsp, (long)parent_tidptr, (long)NULL, tls);
	if (ret<0)
	{
		//couldn't fork
		close(fd);
		return ret;
	}
	else if (ret==0)
	{
		//child
		dup2(fd, FD_PARENT);
		close(fd);
		return ret;
	}
	else // ret>0
	{
		//parent
		close(fd);
		return ret;
	}
}


//errors are returned as -ENOENT, not {errno=ENOENT, -1}; we are (pretend to be) the kernel, not libc
static long syscall_emul(greg_t* regs)
{
//register assignment per http://stackoverflow.com/a/2538212
#define ARG0 (regs[REG_RAX])
#define ARG1 (regs[REG_RDI])
#define ARG2 (regs[REG_RSI])
#define ARG3 (regs[REG_RDX])
#define ARG4 (regs[REG_R10])
#define ARG5 (regs[REG_R8])
#define ARG6 (regs[REG_R9])
#define WRAP0_V(nr, func)                         case nr: return func();
#define WRAP1_V(nr, func, T1)                     case nr: return func((T1)ARG1);
#define WRAP2_V(nr, func, T1, T2)                 case nr: return func((T1)ARG1, (T2)ARG2);
#define WRAP3_V(nr, func, T1, T2, T3)             case nr: return func((T1)ARG1, (T2)ARG2, (T3)ARG3);
#define WRAP4_V(nr, func, T1, T2, T3, T4)         case nr: return func((T1)ARG1, (T2)ARG2, (T3)ARG3, (T4)ARG4);
#define WRAP5_V(nr, func, T1, T2, T3, T4, T5)     case nr: return func((T1)ARG1, (T2)ARG2, (T3)ARG3, (T4)ARG4, (T5)ARG5);
#define WRAP6_V(nr, func, T1, T2, T3, T4, T5, T6) case nr: return func((T1)ARG1, (T2)ARG2, (T3)ARG3, (T4)ARG4, (T5)ARG5, (T6)ARG6);
#define WRAP0(name)                         WRAP0_V(__NR_##name, name)
#define WRAP1(name, T1)                     WRAP1_V(__NR_##name, name, T1)
#define WRAP2(name, T1, T2)                 WRAP2_V(__NR_##name, name, T1, T2)
#define WRAP3(name, T1, T2, T3)             WRAP3_V(__NR_##name, name, T1, T2, T3)
#define WRAP4(name, T1, T2, T3, T4)         WRAP4_V(__NR_##name, name, T1, T2, T3, T4)
#define WRAP5(name, T1, T2, T3, T4, T5)     WRAP5_V(__NR_##name, name, T1, T2, T3, T4, T5)
#define WRAP6(name, T1, T2, T3, T4, T5, T6) WRAP6_V(__NR_##name, name, T1, T2, T3, T4, T5, T6)
#define WRAP0_(name)                         WRAP0_V(__NR_##name, name##_)
#define WRAP1_(name, T1)                     WRAP1_V(__NR_##name, name##_, T1)
#define WRAP2_(name, T1, T2)                 WRAP2_V(__NR_##name, name##_, T1, T2)
#define WRAP3_(name, T1, T2, T3)             WRAP3_V(__NR_##name, name##_, T1, T2, T3)
#define WRAP4_(name, T1, T2, T3, T4)         WRAP4_V(__NR_##name, name##_, T1, T2, T3, T4)
#define WRAP5_(name, T1, T2, T3, T4, T5)     WRAP5_V(__NR_##name, name##_, T1, T2, T3, T4, T5)
#define WRAP6_(name, T1, T2, T3, T4, T5, T6) WRAP6_V(__NR_##name, name##_, T1, T2, T3, T4, T5, T6)
	switch (ARG0)
	{
	WRAP3(open, char*, int, mode_t);
	WRAP2(access, char*, int);
	WRAP2_(stat, char*, struct stat*);
	//treat lstat as stat
	case __NR_lstat: return stat_((char*)ARG1, (struct stat*)ARG2);
	case __NR_fork:  return clone(                         SIGCHLD, 0, NULL, NULL, 0);
	//CLONE_VM && !newsp isn't recommended, but kernel does it so let's follow suit
	case __NR_vfork: return clone(CLONE_VFORK | CLONE_VM | SIGCHLD, 0, NULL, NULL, 0);
	WRAP5(clone, unsigned long, unsigned long, int*, int*, unsigned long);
	default:
		debug("unknown syscall ");
		debugd(ARG0);
		debug("\n");
		exit_group(1);
		return -ENOSYS;
	}
#undef WRAP0_V
#undef WRAP1_V
#undef WRAP2_V
#undef WRAP3_V
#undef WRAP4_V
#undef WRAP5_V
#undef WRAP6_V
#undef WRAP0
#undef WRAP1
#undef WRAP2
#undef WRAP3
#undef WRAP4
#undef WRAP5
#undef WRAP6
#undef WRAP0_
#undef WRAP1_
#undef WRAP2_
#undef WRAP3_
#undef WRAP4_
#undef WRAP5_
#undef WRAP6_
#undef ARG0
#undef ARG1
#undef ARG2
#undef ARG3
#undef ARG4
#undef ARG5
#undef ARG6
}



#define sigaction kabi_sigaction
#undef sa_handler
#undef sa_sigaction
#define SA_RESTORER 0x04000000
struct kabi_sigaction {
	union {
		void (*sa_handler)(int);
		void (*sa_sigaction)(int, siginfo_t*, void*);
	};
	unsigned long sa_flags;
	void (*sa_restorer)(void);
	char sa_mask[_NSIG/8]; // this isn't the correct declaration, but we don't use this field, we only need its size
};

static inline int rt_sigaction(int sig, const struct sigaction * act, struct sigaction * oact, size_t sigsetsize)
{
	return syscall4(__NR_rt_sigaction, sig, (long)act, (long)oact, sigsetsize);
}

static inline void set_sighand(int sig, void(*handler)(int, siginfo_t*, void*))
{
	struct sigaction act;
	act.sa_sigaction = handler;
	//sa_restorer is mandatory; judging by kernel source, this is to allow nonexecutable stack
	//(should've put it in VDSO, but I guess this syscall is older than VDSO)
	act.sa_flags = SA_SIGINFO | SA_RESTORER;
	//and for some reason, I get runtime relocations if I try accessing it from C++, so let's switch language
	__asm__("lea %0, [%%rip+restore_rt]" : "=r"(act.sa_restorer));
	memset(&act.sa_mask, 0, sizeof(act.sa_mask));
	rt_sigaction(sig, &act, NULL, sizeof(act.sa_mask));
}
#define STR_(x) #x
#define STR(x) STR_(x)
__asm__(R"(
#sigaction.sa_restorer takes its arguments from the stack, have to implement it in assembly
#otherwise GCC could do something stupid, like set up a frame pointer
restore_rt:
mov eax, )" STR(__NR_rt_sigreturn) R"(
syscall
)");

static void sa_sigsys(int signo, siginfo_t* info, void* context)
{
	ucontext_t* uctx = (ucontext_t*)context;
	long ret = syscall_emul(uctx->uc_mcontext.gregs);
	uctx->uc_mcontext.gregs[REG_RAX] = ret;
}

extern "C" void preload_action()
{
	set_sighand(SIGSYS, sa_sigsys);
}

}}



#endif

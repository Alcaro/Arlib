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
#include <sys/sysinfo.h>
#include <sys/resource.h>

//gcc recognizes various function names and reads attributes (such as extern) from the headers, force it not to
namespace mysand { namespace {
#undef errno

static const char * progname;


class mutex {
public:
	void lock()
	{
		//TODO
	}
	void unlock()
	{
	}
};
class mutexlocker {
	mutexlocker();
	mutex* m;
public:
	mutexlocker(mutex* m) { this->m = m;  this->m->lock(); }
	~mutexlocker() { this->m->unlock(); }
};


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
	__builtin_unreachable();
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

static inline int fchmod(int fd, mode_t mode)
{
	return syscall2(__NR_fchmod, fd, mode);
}

static inline ssize_t send(int sockfd, const void * buf, size_t len, int flags)
{
	return syscall6(__NR_sendto, sockfd, (long)buf, len, flags, (long)NULL, 0); // no send syscall
}

static inline int dup2(int oldfd, int newfd)
{
	return syscall2(__NR_dup2, oldfd, newfd);
}

#define recvmsg recvmsg_ // gcc claims a few syscalls are ambiguous, the outer (extern) one being as good as this one
static inline ssize_t recvmsg(int sockfd, struct msghdr * msg, int flags)
{
	return syscall3(__NR_recvmsg, sockfd, (long)msg, flags);
}

static inline void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	return (void*)syscall6(__NR_mmap, (long)addr, length, prot, flags, fd, offset);
}

static inline int fcntl(unsigned int fd, unsigned int cmd)
{
	return syscall2(__NR_fcntl, fd, cmd);
}
static inline int fcntl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	return syscall3(__NR_fcntl, fd, cmd, arg);
}


//TODO: free()
void* malloc(size_t size)
{
	//what's the point of brk()
	void* ret = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if ((unsigned long)ret >= (unsigned long)-4095) return NULL;
	return ret;
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
inline void debugs(const char * x)
{
	write(1, x, strlen(x));
}
inline void debugsv(const char * x)
{
	while (*x) write(1, x++, 1);
}


static inline void error(const char * why)
{
	debugs(progname);
	debugs(": ");
	debugs(why);
	debugs("\n");
}
static inline void error(const char * why, int why2)
{
	debugs(progname);
	debugs(": ");
	debugs(why);
	debugd(why2);
	debugs("\n");
}


#include "internal-linux-sand.h"
#define FD_PARENT 3
static mutex broker_mut;
static inline int do_broker_req(broker_req* req)
{
	mutexlocker l(&broker_mut);
	
	send(FD_PARENT, req, sizeof(*req), 0);
	
	//TODO: keep br_send replies around
	broker_rsp rsp;
	int fd;
	ssize_t len = recv_fd(FD_PARENT, &rsp, sizeof(rsp), MSG_CMSG_CLOEXEC, &fd);
	if (len != sizeof(rsp))
	{
		error("socket returned invalid data");
		exit_group(1);
	}
	if (fd >= 0) return fd;
	else if (rsp.err==0) return 0;
	else return -rsp.err;
}

//not sure about the return type, glibc uses int despite buflen being size_t. guess they don't care about 2GB paths
//then neither do I
static inline int getcwd(char* buf, size_t size)
{
	buf[0]='/'; // not even gonna check the length, if size<6 then someone needs slap
	buf[1]='@';
	buf[2]='C';
	buf[3]='W';
	buf[4]='D';
	buf[5]='\0';
	return 6; // returns length including NUL
}

//false for overflow
static bool flatten_path(const char * path, char * outpath, size_t outlen)
{
	char* out_end = outpath+outlen;
	char* outat = outpath;
	if (path[0] != '/')
	{
		int cwdlen = getcwd(outat, out_end-outat);
		if (cwdlen <= 0) return false;
		outat += cwdlen-1;
	}
	*outat++ = '/';
	
	while (*path)
	{
		const char * component = path;
		bool dots = true;
		while (*path!='/' && *path!='\0')
		{
			if (*path!='.') dots=false;
			path++;
		}
		int complen = path-component;
		if (*path) path++;
		
		if (dots && complen==0) continue;
		if (dots && complen==1) continue;
		if (dots && complen==2)
		{
			if (outat != outpath+1)
			{
				outat--;
				while (outat[-1]!='/') outat--;
			}
			continue;
		}
		
		memcpy(outat, component, complen);
		outat += complen;
		
		if (*path) *outat++='/';
		else *outat='\0';
	}
	
	return true;
}

static inline int do_broker_file_req(const char * pathname, broker_req_t op, int flags1 = 0, mode_t flags2 = 0)
{
	broker_req req = { op, { (uint32_t)flags1, flags2 } };
	if (!flatten_path(pathname, req.path, sizeof(req.path))) return -ENOENT;
	return do_broker_req(&req);
}

static inline int open(const char * pathname, int flags, mode_t mode = 0)
{
	int fd = do_broker_file_req(pathname, br_open, flags, mode);
	if (fd>=0 && !(flags&O_CLOEXEC)) fcntl(fd, F_SETFD, fcntl(fd, F_GETFD)&~O_CLOEXEC);
	return fd;
}

static inline int unlink(const char * pathname)
{
	return do_broker_file_req(pathname, br_unlink);
}

static inline int access(const char * pathname, int flags)
{
	return do_broker_file_req(pathname, br_access, flags);
}

//explicit underscore because #define stat stat_ messes up the struct
static inline int stat_(const char * pathname, struct stat * buf)
{
	int fd = open(pathname, O_RDONLY);
	if (fd<0) return fd;
	
	int ret = fstat(fd, buf);
	close(fd);
	return ret;
}

static inline int chmod(const char * pathname, mode_t mode)
{
	int fd = open(pathname, O_RDONLY);
	if (fd<0) return fd;
	
	int ret = fchmod(fd, mode);
	close(fd);
	return ret;
}

static inline pid_t clone(unsigned long clone_flags, unsigned long newsp,
                          int* parent_tidptr, int* child_tidptr, unsigned long tls)
{
	//the bpf filter allows clone with CLONE_THREAD set, so this doesn't block any usual use of SETTID
	if (clone_flags&CLONE_CHILD_SETTID) return -EINVAL;
	
	broker_req req = { br_fork };
	int fd = do_broker_req(&req);
	if (fd<0) return -ENOMEM; // probably accurate enough
	
	pid_t ret = syscall5(__NR_clone, clone_flags|CLONE_CHILD_SETTID, newsp, (long)parent_tidptr, (long)NULL, tls);
	//pid_t ret = syscall5(__NR_clone, clone_flags, newsp, (long)parent_tidptr, (long)0x12345678, tls);
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

static inline int execveat(int dirfd, const char * pathname, char * const argv[], char * const envp[], int flags)
{
	//TODO: this leaks the argv malloc on failure
	
	if (dirfd != AT_FDCWD) return -ENOSYS;
	if (flags & ~AT_EMPTY_PATH) return -ENOSYS;
	
	int access_ok = access(pathname, X_OK);
	if (access_ok < 0) return access_ok;
	
	int n_argv = 0;
	while (argv[n_argv]) n_argv++;
	
	char** new_argv = (char**)malloc(sizeof(char*)*(1+n_argv+1)); // +1 for ld-linux.so, +1 for NULL
	if (!new_argv) return -ENOMEM;
	new_argv[0] = (char*)"/lib64/ld-linux-x86-64.so.2";
	new_argv[1] = (char*)pathname;
	for (int i=1;i<=n_argv;i++)
	{
		new_argv[1+i] = argv[i];
	}
	
	broker_req req = { br_get_emul };
	int fd = do_broker_req(&req);
	if (fd<0) return -ENOMEM;
	
	const char * execveat_gate = (char*)0x00007FFFFFFFEFFF;
	const char * execveat_gate_page = (char*)((long)execveat_gate&~0xFFF);
	mmap((void*)execveat_gate_page, 4096, PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
	
	return syscall5(__NR_execveat, fd, (long)execveat_gate, (long)new_argv, (long)envp, AT_EMPTY_PATH);
}

static inline int execve(const char * filename, char * const argv[], char * const envp[])
{
	return execveat(AT_FDCWD, filename, argv, envp, 0);
}

//this one is used to find amount of system RAM, for tuning gcc's garbage collector
//stacktrace: __get_phys_pages(), sysconf(name=_SC_PHYS_PAGES), physmem_total(), init_ggc_heuristics()
//(__get_phys_pages() also needs page size, but that's constant, 4096)
//don't bother giving it the real values, just give it something that looks tasty
static inline int sysinfo_(struct sysinfo * info)
{
	memset(info, 0, sizeof(info));
	info->uptime = 0;
	info->loads[0] = 0;
	info->loads[1] = 0;
	info->loads[2] = 0;
	info->totalram = 4ULL*1024*1024*1024;
	info->freeram = 4ULL*1024*1024*1024;
	info->sharedram = 0;
	info->bufferram = 0;
	info->totalswap = 0;
	info->freeswap = 0;
	info->procs = 1;
	info->totalhigh = 0;
	info->freehigh = 0;
	info->mem_unit = 1;
	return 0;
}

#define getrusage getrusage_
static inline int getrusage(int who, struct rusage * usage)
{
	memset(usage, 0, sizeof(*usage));
	return 0;
}


//errors are returned as -ENOENT, not {errno=ENOENT, -1}; we are (pretend to be) the kernel, not libc
static long syscall_emul(greg_t* regs, int errno)
{
//register assignment per http://stackoverflow.com/a/2538212
#define ARG0 (regs[REG_RAX])
#define ARG1 (regs[REG_RDI])
#define ARG2 (regs[REG_RSI])
#define ARG3 (regs[REG_RDX])
#define ARG4 (regs[REG_R10])
#define ARG5 (regs[REG_R8])
#define ARG6 (regs[REG_R9])
	if (errno != 0)
	{
		error("denied syscall ", ARG0);
		return -errno;
	}
	
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
	case __NR_fork:  return clone(SIGCHLD, 0, NULL, NULL, 0);
	//vfork manpage:
	//  the behavior is undefined if the process created by
	//  vfork() either modifies any data other than a variable of type pid_t
	//  used to store the return value from vfork(), or returns from the
	//  function in which vfork() was called, or calls any other function
	//  before successfully calling _exit(2) or one of the exec(3) family of
	//  functions
	//this one returns all the way out of a signal handler,
	// and execve() allocates memory and does about a dozen syscalls
	//therefore, implement vfork as normal fork
	//(should've made posix_spawn a syscall)
	case __NR_vfork: return clone(SIGCHLD, 0, NULL, NULL, 0);
	WRAP3(execve, char*, char**, char**);
	WRAP5(clone, unsigned long, unsigned long, int*, int*, unsigned long);
	WRAP1_(sysinfo, struct sysinfo*);
	WRAP2(getcwd, char*, unsigned long);
	WRAP1(unlink, char*);
	WRAP2(getrusage, int, struct rusage*);
	WRAP2(chmod, char*, mode_t);
	default:
		error("can't emulate syscall ", ARG0);
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

static inline int rt_sigprocmask(int how, const sigset_t* nset, sigset_t* oset, size_t sigsetsize)
{
	return syscall4(__NR_rt_sigprocmask, how, (long)nset, (long)oset, sigsetsize);
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
	long ret = syscall_emul(uctx->uc_mcontext.gregs, info->si_errno);
	uctx->uc_mcontext.gregs[REG_RAX] = ret;
}

//no functions allowed, more inlines
#define sigemptyset __sigemptyset // from sigset.h
#define sigaddset __sigaddset // this one had to be copied
#define __sigaddset __sigaddset_ // go away, extern function

#define __SIGSETFN(NAME, BODY, CONST)					      \
  static inline int							      \
  NAME (CONST __sigset_t *__set, int __sig)				      \
  {									      \
    unsigned long int __mask = __sigmask (__sig);			      \
    unsigned long int __word = __sigword (__sig);			      \
    return BODY;							      \
  }

__SIGSETFN (__sigismember, (__set->__val[__word] & __mask) ? 1 : 0, const)
__SIGSETFN (__sigaddset, ((__set->__val[__word] |= __mask), 0), )
__SIGSETFN (__sigdelset, ((__set->__val[__word] &= ~__mask), 0), )

static void unblock_signal(int signo)
{
	//if the child calls execveat, it does so in a SIGSYS handler
	//that means SIGSYS is blocked, because recursion is evil and recursive signals are even worse
	//but once we call execveat, we'll leave the SIGSYS handler - but kernel doesn't realize that
	//let's just unblock it manually
	//(strange how SIGSYS isn't beside SIGBUS, SIGFPE, SIGILL, SIGSEGV in the rt_sigprocmask(2) manpage)
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGSYS);
	rt_sigprocmask(SIG_UNBLOCK, &set, NULL, _NSIG/8);
}

extern "C" void preload_action(char** argv)
{
	set_sighand(SIGSYS, sa_sigsys);
	unblock_signal(SIGSYS);
	
	progname = argv[1];
}

}}



#endif

#ifdef __linux__
#ifndef ARLIB_TEST
#include "sandbox.h"
#include "../process.h"
#include "../test.h"

#include <sys/syscall.h>
#ifdef __NR_execveat

#include <sys/user.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <linux/memfd.h> // documented as sys/memfd.h, but that doesn't exist
#include <fcntl.h>
#define F_LINUX_SPECIFIC_BASE 1024
#define F_ADD_SEALS   (F_LINUX_SPECIFIC_BASE + 9)  // and these only exist in linux/fcntl.h - where fcntl() doesn't exist
#define F_GET_SEALS   (F_LINUX_SPECIFIC_BASE + 10) // and I can't include both, duplicate definitions
#define F_SEAL_SEAL   0x0001
#define F_SEAL_SHRINK 0x0002
#define F_SEAL_GROW   0x0004
#define F_SEAL_WRITE  0x0008
#include <sys/prctl.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <linux/audit.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>

#include "internal-linux-sand.h"

/*
//ensure a AF_UNIX SOCK_SEQPACKET socketpair can't specify dest_addr
//(turns out it can, but address is ignored. good enough)
void stest()
{
	//const int type = SOCK_DGRAM;
	const int type = SOCK_SEQPACKET;
	//const int type = SOCK_STREAM;
	
	int socks[2];
	socketpair(AF_UNIX, type, 0, socks);
	
	struct sockaddr_un ga = { AF_UNIX, "socket" };
	int gal = offsetof(sockaddr_un,sun_path) + 7;
	int g = socket(AF_UNIX, type, 0);
	errno=0;
	bind(g, (sockaddr*)&ga, gal);
	perror("bind");
	listen(g, 10);
	perror("listen");
	
	struct sockaddr_un ga2 = { AF_UNIX, "\0socket" };
	int ga2l = offsetof(sockaddr_un,sun_path) + 8;
	int g2 = socket(AF_UNIX, type, 0);
	errno=0;
	bind(g2, (sockaddr*)&ga2, ga2l);
	perror("bind");
	listen(g2, 10);
	perror("listen");
	
	//int g3 = socket(AF_UNIX, type, 0);
	int g3 = socks[1];
	errno=0;
	sendto(g3, "foo",3, 0, (sockaddr*)&ga, gal);
	perror("sendto");
	errno=0;
	sendto(g3, "bar",3, 0, (sockaddr*)&ga2, ga2l);
	perror("sendto");
	
	while (true)
	{
		char out[6];
		int n = recv(socks[0], out, 6, MSG_DONTWAIT);
		if (n<0) break;
		n = write(1, out, n);
	}
	
	close(socks[0]);
	close(socks[1]);
	//close(g);
	//close(g2);
	if (g3!=socks[1]) close(g3);
	unlink("socket");
}
// */


template<typename T> inline T require(T x)
{
	//failures are easiest debugged with strace
	if ((long)x == -1) _exit(1);
	return x;
}
//this could be an overload, but for security-critical code, explicit is better than implicit
inline void require_b(bool expected)
{
	if (!expected) _exit(1);
}
template<typename T> inline T require_eq(T actual, T expected)
{
	if (actual != expected) _exit(1);
	return actual;
}


extern const char sandbox_preload_bin[];
extern const unsigned sandbox_preload_len;

int preloader_fd()
{
	static int s_fd=0;
	if (s_fd) return s_fd;
	
	int fd = syscall(__NR_memfd_create, "arlib-sand-preload", MFD_CLOEXEC|MFD_ALLOW_SEALING);
	if (fd<0)
		goto fail;
	if (write(fd, sandbox_preload_bin, sandbox_preload_len) != sandbox_preload_len)
		goto fail;
	if (fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE|F_SEAL_SHRINK|F_SEAL_GROW|F_SEAL_SEAL) < 0)
		goto fail;
	int prev;
	prev = lock_cmpxchg(&s_fd, 0, fd);
	if (prev != 0)
	{
		close(fd);
		return prev;
	}
	return fd;
	
fail:
	if (fd>=0) close(fd);
	return -1;
}


//atoi is locale-aware, not gonna trust that to not call malloc or otherwise be stupid
static int atoi_simple(const char * text)
{
	int ret = 0;
	while (*text)
	{
		if (*text<'0' || *text>'9') return -1;
		ret *= 10;
		ret += *text-'0';
		text++;
	}
	return ret;
}

//trusting everything to set O_CLOEXEC isn't enough, this is a sandbox
//throws about fifty errors in Valgrind for fd 1024 - don't care
//lowfd is the LOWEST fd that IS closed; alternatively, it's the NUMBER of fds kept
static bool closefrom(int lowfd)
{
	//getdents[64] is documented do-not-use and opendir should be used instead.
	//However, we're in (the equivalent of) a signal handler, and opendir is not signal safe.
	//Therefore, raw kernel interface it is.
	struct linux_dirent64 {
		ino64_t        d_ino;    /* 64-bit inode number */
		off64_t        d_off;    /* 64-bit offset to next structure */
		unsigned short d_reclen; /* Size of this dirent */
		unsigned char  d_type;   /* File type */
		char           d_name[]; /* Filename (null-terminated) */
	};
	
	int dfd = open("/proc/self/fd/", O_RDONLY|O_DIRECTORY);
	if (dfd < 0) return false;
	
	while (true)
	{
		char bytes[1024];
		// this interface always returns full structs, no need to paste things together
		int nbytes = syscall(SYS_getdents64, dfd, &bytes, sizeof(bytes));
		if (nbytes < 0) { close(dfd); return false; }
		if (nbytes == 0) break;
		
		int off = 0;
		while (off < nbytes)
		{
			linux_dirent64* dent = (linux_dirent64*)(bytes+off);
			off += dent->d_reclen;
			
			int fd = atoi_simple(dent->d_name);
			if (fd>=0 && fd!=dfd && fd>=lowfd)
			{
				close(fd);
			}
		}
	}
	close(dfd);
	return true;
}


static bool install_seccomp()
{
	static const struct sock_filter filter[] = {
		#include "bpf.inc"
	};
	static_assert(sizeof(filter)/sizeof(filter[0]) < 65536);
	static const struct sock_fprog prog = {
		.len = (unsigned short)(sizeof(filter)/sizeof(filter[0])),
		.filter = (sock_filter*)filter,
	};
	require(prctl(PR_SET_NO_NEW_PRIVS, 1, 0,0,0));
	require(prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog));
	return true;
}


static int execveat(int dirfd, const char * pathname, char * const argv[], char * const envp[], int flags)
{
	return syscall(__NR_execveat, dirfd, pathname, argv, envp, flags);
}


bool boot_sand(char** argv, char** envp, pid_t& pid, int& sock)
{
	//fcntl is banned by seccomp, so this goes early
	//putting it before clone() allows sharing it between sandbox children
	int preld_fd = preloader_fd();
	if (preld_fd<0)
		return false;
	
	int socks[2];
	if (socketpair(AF_UNIX, SOCK_SEQPACKET|SOCK_CLOEXEC, 0, socks)<0)
		return false;
	
	int clone_flags = CLONE_NEWUSER | CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWCGROUP | CLONE_NEWIPC | CLONE_NEWNS | CLONE_NEWUTS;
	clone_flags |= SIGCHLD; // parent termination signal, must be SIGCHLD for waitpid to work properly
	int ret = syscall(__NR_clone, clone_flags, NULL, NULL, NULL, NULL);
	if (ret<0) return false;
	if (ret>0)
	{
		pid = ret;
		sock = socks[0];
		close(socks[1]);
		return true;
	}
	
	//we're the child
	
	//some of these steps depend on each other, don't swap them randomly
	
	//we could request preld from parent, but this is easier
	close(socks[0]);
	if (preld_fd == 3) preld_fd = require(dup(preld_fd)); // don't bother closing this, it's done by that dup()
	if (socks[1] != 3) require(dup2(socks[1], 3));
	if (preld_fd != 4) require(dup2(preld_fd, 4));
	
	//remove cloexec on socket
	fcntl(3, F_SETFD, 0);
	//emulator fd is also cloexec, keep it
	
	//wipe unexpected fds, including the duplicates of socks[1] and preld_fd
	if (!closefrom(5)) require(-1);
	
	struct rlimit rlim_fsize = { 8*1024*1024, 8*1024*1024 };
	require(setrlimit(RLIMIT_FSIZE, &rlim_fsize));
	
	//CLONE_NEWUSER doesn't seem to grant access to cgroups
	//once (if) it does, set these:
	// memory.memsw.limit_in_bytes = 100*1024*1024
	// cpu.cfs_period_us = 100*1000, cpu.cfs_quota_us = 50*1000
	// pids.max = 10
	//for now, just stick up some rlimit rules, to disable the most naive forkbombs or memory wastes
	
	struct rlimit rlim_as = { 1*1024*1024*1024, 1*1024*1024*1024 }; // this is the only one that affects mmap
	require(setrlimit(RLIMIT_AS, &rlim_as));
	
	//why so many? because the rest of the pid is also included, which is often a few hundred
	//http://elixir.free-electrons.com/linux/latest/source/kernel/fork.c#L1564
	struct rlimit rlim_nproc = { 500, 500 };
	require(setrlimit(RLIMIT_NPROC, &rlim_nproc));
	
	//die on parent death
	require(prctl(PR_SET_PDEATHSIG, SIGKILL));
	struct broker_req req = { br_nop };
	//ensure parent is still alive; if it's not, this fails with EPIPE (and SIGPIPE, but our caller may have ignored that)
	require(send(3, &req, sizeof(req), MSG_EOR));
	
	//revoke filesystem
	require(chroot("/proc/sys/debug/"));
	require(chdir("/"));
	
	require_eq(install_seccomp(), true);
	
	static const char * const new_envp[] = {
		"TERM=xterm", // some programs check this to know whether they can color, some check ioctl(TCGETS), some check both
		"PATH=/usr/bin:/bin",
		"TMPDIR=/tmp",
		"LANG=en_US.UTF-8",
		NULL
	};
	
	//no idea why 0x00007FFF'FFFFF000 isn't mappable, but sure, we don't care what the last page is as long as there is one
	char* final_page = (char*)0x00007FFFFFFFE000;
	require_eq(mmap(final_page+0x1000, 0x1000, PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0), MAP_FAILED);
	require_eq(mmap(final_page,        0x1000, PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0), (void*)final_page);
	require(execveat(4, final_page+0xFFF, argv, (char**)new_envp, AT_EMPTY_PATH));
	
	_exit(1); // execve never returns nonnegative, and require never returns from negative, but gcc knows neither
}


void sand_do_the_thing(int pid, int sock);
int main(int argc, char ** argv, char ** envp)
{
	int pid;
	int sock;
	if (!boot_sand(argv, envp, pid, sock)) return 1;
	
	sand_do_the_thing(pid, sock);
	return 0;
}

/*
bool sandproc::launch_impl(cstring path, arrayview<string> args)
{
	this->preexec = bind_this(&sandproc::preexec_fn);
	bool ret = process::launch_impl(path, args);
	if (!ret) return false;
	ptrace(PTRACE_SETOPTIONS, this->pid, NULL, (uintptr_t)(PTRACE_O_EXITKILL | PTRACE_O_TRACESECCOMP));
	ptrace(PTRACE_CONT, this->pid, NULL, (uintptr_t)0);
	return true;
}

void sandproc::preexec_fn(execparm* params)
{
	if (conn)
	{
		params->nfds_keep = 4;
		//dup2(conn->?, 3);
	}
	
	
	ptrace(PTRACE_TRACEME);
	puts("AAAAAAAAAAA");
	
	//FIXME: seccomp script
	//note that ftruncate() must be rejected on kernel < 3.17 (including Ubuntu 14.04), to ensure sandcomm::shalloc can't SIGBUS
	//linkat() must also be restricted, to ensure open(O_TRUNC) and truncate() can't be used
}

//https://github.com/nelhage/ministrace/commit/0a1ab993f4763e9fb77d9a89e763a403d80de1ff

//void* sandproc::ptrace_func_c(void* arg) { ((sandproc*)arg)->ptrace_func(); return NULL; }
//void sandproc::ptrace_func()
//{
//	//glibc does
//	//void* stack = mmap(NULL, 256*1024, size, prot, MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0);
//	//clone(flags=(CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGNAL |
//	//             CLONE_SETTLS | CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID | CLONE_SYSVSEM | 0)
//}

sandproc::~sandproc() { delete this->conn; }

//static void sandtest_fn(sandcomm* comm)
//{
	//int* i = comm->malloc<int>();
	//comm->wait(0);
	//(*i)++;
	//comm->release(1);
//}

//test()
//{
//	test_skip("not implemented");
//	{
//		sandproc p;
//		bool ok = p.launch("/bin/true");
//		assert(ok);
//		int status;
//		p.wait(&status);
//		assert_eq(status, 0);
//	}
//	
//	{
//		sandproc p[5];
//		for (int i=0;i<5;i++) assert(p[i].launch("/bin/sleep", "5"));
//		for (int i=0;i<5;i++)
//		{
//			int ret;
//			p[i].wait(&ret);
//			assert_eq(ret, 0);
//		}
//	}
//	
//	//{
//		//sandproc p;
//		//p.error();
//		//p.permit("/etc/not_passwd");
//		//p.permit("/etc/passw");
//		//p.permit("/etc/passwd/");
//		//p.permit("/etc/passwd_");
//		//assert(p.launch("/bin/cat", "/etc/passwd"));
//		//p.wait();
//		//assert_eq(p.read(), "");
//		//assert(p.error() != "");
//	//}
//	
//	//{
//		//sandproc p;
//		//p.error();
//		//p.permit("/etc/passwd");
//		//assert(p.launch("/bin/cat", "/etc/passwd"));
//		//p.wait();
//		//assert(p.read() != "");
//		//assert_eq(p.error(), "");
//	//}
//	
//	//{
//		//TODO: call sandfunc::enter in test.cpp
//		//sandfunc f;
//		//sandcomm* comm = f.launch(sandtest_fn);
//		//int* i = comm->malloc<int>();
//		//assert(i);
//		//*i = 1;
//		//comm->release(0);
//		//comm->wait(1);
//		//assert_eq(*i, 2);
//		//comm->free(i);
//	//}
//	
//}
*/
#endif
#endif
#endif

// This program is intended to be setuid. It sets up some namespaces, a seccomp filter, and various other security restrictions.
// It contains zero input-dependent branches; the only input it takes at all is kernel error codes and return values,
//  reading and discarding one byte from a socket, and passing argv to execveat() once all restrictions are in place.
// It is only 250 lines of code, and does not call into any other file, other than libc.
// Ideally, it wouldn't be needed, but as long as user namespaces are root-only on some distros, those distros need this thing.
// If unprivileged user namespaces are enabled on your distro, this binary won't be used and can safely be removed.

__attribute__((noreturn))
void sandbox_exec_lockdown(const char * const * argv);

#include <stddef.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/sched.h>
#include <linux/seccomp.h>
#include "internal-linux-sand.h"

//#include <linux/ioprio.h> // ioprio_set - header doesn't exist for me, copying the content
//I believe they count as userspace ABI, i.e. no meaningful license constraints

#define IOPRIO_CLASS_SHIFT	(13)
#define IOPRIO_PRIO_MASK	((1UL << IOPRIO_CLASS_SHIFT) - 1)
#define IOPRIO_PRIO_CLASS(mask)	((mask) >> IOPRIO_CLASS_SHIFT)
#define IOPRIO_PRIO_DATA(mask)	((mask) & IOPRIO_PRIO_MASK)
#define IOPRIO_PRIO_VALUE(class, data)	(((class) << IOPRIO_CLASS_SHIFT) | data)

enum {
	IOPRIO_CLASS_NONE,
	IOPRIO_CLASS_RT,
	IOPRIO_CLASS_BE,
	IOPRIO_CLASS_IDLE,
};

enum {
	IOPRIO_WHO_PROCESS = 1,
	IOPRIO_WHO_PGRP,
	IOPRIO_WHO_USER,
};

#ifdef SANDBOX_SETUID
static int req_fail_fd = -1;
#endif
//these could be overloads, but for security-critical code, explicit is good
inline void require_b(bool expected)
{
	if (!expected)
	{
#ifdef SANDBOX_SETUID
		// a message of length != sizeof(pid_t) to tell parent that something failed
		// can't just terminate; the wrapper's child will keep the socket alive, causing a deadlock
		// doesn't apply to failure before calling clone(); if the aforementioned child doesn't exist, it can't hold anything alive
		if (req_fail_fd != -1) send(req_fail_fd, "", 1, 0);
#endif
		_exit(1);
	}
}
template<typename T> inline T require(T x)
{
	//failures are easiest debugged with strace
	require_b((long)x != -1);
	return x;
}
template<typename T> inline T require_eq(T actual, T expected)
{
	require_b(actual == expected);
	return actual;
}

static bool install_seccomp()
{
	static const struct sock_filter filter[] = {
		#include "bpf.inc"
	};
	constexpr size_t filter_len = sizeof(filter)/sizeof(*filter);
	static_assert(filter_len == (unsigned short)filter_len);
	static const struct sock_fprog prog = {
		.len = (unsigned short)filter_len,
		.filter = (sock_filter*)filter,
	};
	require(prctl(PR_SET_NO_NEW_PRIVS, 1, 0,0,0));
	require(prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog));
	return true;
}

// no glibc wrapper until glibc 2.34 (august 2021)
static int execveat(int dirfd, const char * pathname, const char * const argv[], const char * const envp[], int flags)
{
	return syscall(__NR_execveat, dirfd, pathname, argv, envp, flags);
}


void sandbox_exec_lockdown(const char * const * argv)
{
	//WARNING:
	//fork(), POSIX.1-2008, http://pubs.opengroup.org/onlinepubs/9699919799/functions/fork.html
	//  If a multi-threaded process calls fork(), the new process shall contain a replica of the
	//  calling thread and its entire address space, possibly including the states of mutexes and
	//  other resources. Consequently, to avoid errors, the child process may only execute
	//  async-signal-safe operations until such time as one of the exec functions is called.
	//This applies to clone() too. In particular, malloc must be avoided. Syscalls and stack is fine.
	//It doesn't apply to the setuid version, but this file is used by both.
	
	//some of these steps depend on each other, don't swap them randomly
	
	struct rlimit rlim_fsize = { 8*1024*1024, 8*1024*1024 };
	require(setrlimit(RLIMIT_FSIZE, &rlim_fsize));
	
	//CLONE_NEWUSER doesn't seem to grant access to cgroups
	//once (if) it does, set these:
	// memory.memsw.limit_in_bytes = 100*1024*1024
	// cpu.cfs_period_us = 100*1000, cpu.cfs_quota_us = 50*1000
	// pids.max = 30
	//for now, just stick up some rlimit rules, to disable the most naive forkbombs or memory wastes
	
	struct rlimit rlim_as = { 1*1024*1024*1024, 1*1024*1024*1024 }; // this is the only one that affects mmap
	require(setrlimit(RLIMIT_AS, &rlim_as)); // keep the value synced with sysinfo() in sysemu
	
	//why so many? because the rest of the root-namespace user, including threads, is also included, which is often several hundred
	//https://elixir.bootlin.com/linux/v5.10.9/source/kernel/fork.c#L1964
	struct rlimit rlim_nproc = { 1000, 1000 };
	require(setrlimit(RLIMIT_NPROC, &rlim_nproc));
	
	//nice value
	//PRIO_PROCESS,0 - calling thread
	//19 - lowest possible priority
	require(setpriority(PRIO_PROCESS,0, 19));
	//ionice
	//IOPRIO_PRIO_VALUE(IOPRIO_CLASS_IDLE, 0) - lowest priority; the 0 is unused as of kernel 4.16
	require(syscall(__NR_ioprio_set, IOPRIO_WHO_PROCESS,0, IOPRIO_PRIO_VALUE(IOPRIO_CLASS_IDLE, 0)));
	
	//die on parent death
	//('parent' is parent thread, not the entire process, but Arlib process objects have thread affinity anyways)
	require(prctl(PR_SET_PDEATHSIG, SIGKILL));
	
	//ensure parent is still alive
	//have to check for a response, it's possible that parent died before the prctl but its socket isn't deleted yet
	struct broker_req req = { br_ping };
	require_eq(send(FD_BROKER, &req, sizeof(req), MSG_NOSIGNAL|MSG_EOR), (ssize_t)sizeof(req));
	struct broker_rsp rsp;
	require_eq(recv(FD_BROKER, &rsp, sizeof(rsp), MSG_NOSIGNAL), (ssize_t)sizeof(rsp));
	//discard the response, we know it's { br_ping, {0,0,0}, "" }, we only care whether we got one at all
	
	// every syscall using uids is blocked by seccomp, but being root shows up in ps -ef, and may affect scheduling, so drop it if possible
	// applies to both setuid and sudo
	require_b(setuid(65534)==0 || errno==EINVAL); // EINVAL means uid_map wasn't set, meaning sandbox isn't root and doesn't need a setuid()
	require_b(setgid(65534)==0 || errno==EINVAL); // pointless but harmless
	
	//revoke filesystem
	//ideally there'd be a completely empty directory somewhere, but I can't find one
	// (only affects execveat .interp, every other filesystem syscall is blocked by seccomp)
	require(chroot("/proc/sys/debug/"));
	require(chdir("/"));
	
	require_b(install_seccomp());
	
	static const char * const new_envp[] = {
		"TERM=xterm", // some programs check this to know whether they can color, some check ioctl(TCGETS), some check both
		"PATH=/usr/bin:/bin",
		"TMPDIR=/tmp",
		"LANG=en_US.UTF-8",
		NULL
	};
	
	//page 0x00007FFF'FFFFFnnn isn't mappable, apparently sticking SYSCALL (0F 05) at 0x00007FFF'FFFFFFFE
	// will return to an invalid address and blow up
	//http://elixir.free-electrons.com/linux/v4.11/source/arch/x86/include/asm/processor.h#L832
	//doesn't matter what the last page is, as long as there is one
	//make sure to edit the BPF rules if changing this
	//(fair chance it's gonna break once five-level paging shows up)
	char* final_page = (char*)0x00007FFFFFFFE000;
	require_eq(mmap(final_page+0x1000, 0x1000, PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0), MAP_FAILED);
	require_eq(mmap(final_page,        0x1000, PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0), (void*)final_page);
	
	require(execveat(FD_EMUL, final_page+0xFFF, argv, new_envp, AT_EMPTY_PATH));
	
	__builtin_trap(); // execve never returns nonnegative, and require never returns from negative, but gcc knows neither
}

#ifdef SANDBOX_SETUID
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv)
{
	if (geteuid() != 0) exit(1);
	
	int clone_flags = CLONE_NEWUSER | CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWCGROUP | CLONE_NEWIPC | CLONE_NEWNS | CLONE_NEWUTS;
	clone_flags |= SIGCHLD; // termination signal, must be SIGCHLD for waitpid to work properly
	clone_flags |= CLONE_PARENT; // cloning the child's ppid allows the current process to terminate after setting up the child
	pid_t pid = syscall(__NR_clone, clone_flags, NULL, NULL, NULL, NULL);
	
	if (pid < 0) exit(1);
	if (pid > 0)
	{
		// parent path
		req_fail_fd = FD_BROKER;
		
		// set uid_map, so the process drops back down to the caller's uid
		// the sandboxed process can't use its uid, but it looks weird in ps, and I'm not sure if you can kill() a setuid program
		
		// a process can (as far as I can see) not set its own uid_map, so let parent do that
		// docs say it can, but testing just yields -EPERM
		
		char filename[32]; // like above; the rest of the string is 16 (nul included), but 10+16 <= 32
		sprintf(filename, "/proc/%d/uid_map", pid);
		int fd = require(open(filename, O_WRONLY));
		char uid_map[32]; // uid_t is int, INT_MIN is length 10, the rest of the string is 9 (including the nul), 10+9 <= 32
		sprintf(uid_map, "65534 %d 1", getuid()); // fprintf would be easier, but it's harder to check error codes on that
		require_eq(write(fd, uid_map, strlen(uid_map)), (ssize_t)strlen(uid_map));
		require(close(fd));
		
		send(FD_BROKER, &pid, sizeof(pid_t), 0); // this is received in launch-linux-sand.cpp
		exit(0); // the actual exit status doesn't matter, parent asked for no SIGCHLD
	}
	if (pid == 0)
	{
		// child path
		
		// wait for the above branch to finish, so setuid succeeds
		// and for launcher to receive the pid from the parent process, so its { br_ping } doesn't do anything silly
		char discard[1];
		require_eq(recv(FD_BROKER, &discard, 1, 0), (ssize_t)1); // sent by launch-linux-sand.cpp after the pid is sent
		require(setuid(65534));
		
		sandbox_exec_lockdown(argv);
	}
}
#endif

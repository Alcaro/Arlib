#include "sandbox.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <linux/memfd.h> // documented as sys/memfd.h, but that file doesn't exist
#include <linux/sched.h>
#include "internal.h"

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


extern const char sandbox_preload_bin[];
extern const unsigned sandbox_preload_len;

int sandproc::preloader_fd()
{
	static int s_fd = 0;
	if (lock_read<lock_loose>(&s_fd)) return s_fd;
	
	int fd = syscall(__NR_memfd_create, "gvc-preload", MFD_CLOEXEC|MFD_ALLOW_SEALING);
	if (fd < 0)
		goto fail;
	if (write(fd, sandbox_preload_bin, sandbox_preload_len) != sandbox_preload_len)
		goto fail;
	if (fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE|F_SEAL_SHRINK|F_SEAL_GROW|F_SEAL_SEAL) < 0)
		goto fail;
	int prev;
	prev = lock_cmpxchg<lock_loose, lock_loose>(&s_fd, 0, fd);
	if (prev != 0)
	{
		close(fd);
		return prev;
	}
	return fd;
	
fail:
	if (fd >= 0) close(fd);
	return -1;
}


template<typename T> inline T require(T x)
{
	//failures are easiest debugged with strace
	if ((long)x == -1) _exit(1);
	return x;
}
//this could be an overload, but for security-critical code, explicit is good
inline void require_b(bool expected)
{
	if (!expected) _exit(1);
}
template<typename T> inline T require_eq(T actual, T expected)
{
	if (actual != expected) _exit(1);
	return actual;
}

static ssize_t send_repeat(int sockfd, const void * buf, size_t len, int flags)
{
again:
	ssize_t ret = send(sockfd, buf, len, flags);
	if (ret == -1 && errno == EINTR) goto again;
	return ret;
}

__attribute__((noreturn))
void sandbox_exec_lockdown(const char * const * argv);

void sandproc::launch_impl(const char * program, array<const char*> argv, array<int> stdio_fd)
{
	// can't override argv[0] in sandbox, ld-linux won't understand that
	// if you need a fake argv[0], redirect the chosen executable via sandbox policy
	argv[0] = program;
	
	argv.insert(0, "[gravelcube]"); // ld-linux thinks it's argv[0] and discards the real one
	
	//fcntl is banned by seccomp, so this goes early
	//putting it before clone() allows sharing it between sandbox children
	int preld_fd = preloader_fd();
	if (preld_fd < 0)
		return;
	
	int socks[2];
	if (socketpair(AF_UNIX, SOCK_SEQPACKET|SOCK_CLOEXEC, 0, socks) < 0)
		return;
	
	stdio_fd.append(socks[1]);
	stdio_fd.append(preld_fd); // we could request preld from parent, but this is easier
	
	int clone_flags = CLONE_NEWUSER | CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWCGROUP | CLONE_NEWIPC | CLONE_NEWNS | CLONE_NEWUTS;
	clone_flags |= CLONE_PIDFD;
#ifdef __x86_64__
	pid_t pid = syscall(__NR_clone, clone_flags, NULL, &this->pidfd, NULL, NULL);
#endif
	if (pid < 0)
	{
		if (errno == EPERM)
		{
			int socks2[2];
			if (socketpair(AF_UNIX, SOCK_SEQPACKET|SOCK_CLOEXEC, 0, socks2) < 0)
				return;
			
			string setuid_path = file::exedir()+"gvc-setuid"; // do this before clone(), malloc isn't signal handler safe
			pid_t pid_setuid = syscall(__NR_clone, 0, NULL, NULL, NULL, NULL); // clone instead of fork to discard the SIGCHLD
			if (pid_setuid < 0) {} // do nothing, fall through to the errno != EPERM clause
			if (pid_setuid > 0)
			{
				// parent path
				mainsock = socks[0];
				watch_add(socks[0]);
				close(socks[1]);
				
				char dummy[1];
				if (recv_fd(socks[0], dummy, 1, 0, &this->pidfd) != 1) return;
				if (this->pidfd < 0) return;
				if (send_repeat(socks[0], "", 1, 0) != (ssize_t)1) { terminate(); return; }
				
				return;
			}
			if (pid_setuid == 0)
			{
				// child path
				require_b(set_fds(stdio_fd)); // easier before exec than after, and more code here and less in setuid is more secure
				require(execve(setuid_path, (char**)argv.ptr(), NULL));
				__builtin_trap(); // unreachable
			}
		}
		close(socks[0]);
		close(socks[1]);
		return;
	}
	if (pid > 0)
	{
		// parent path
		mainsock = socks[0];
		watch_add(socks[0]);
		close(socks[1]);
		return;
	}
	
	// child path
	require_b(set_fds(stdio_fd));
	
	sandbox_exec_lockdown(argv.ptr());
}

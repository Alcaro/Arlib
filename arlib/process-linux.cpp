#include "process.h"

#ifdef __linux__
#include <spawn.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <signal.h>
#include <limits.h>

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
bool process::closefrom(int lowfd)
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
#ifdef ARLIB_TEST_ARLIB
				if (fd >= 1024) continue; // shut up Valgrind
#endif
				close(fd);
			}
		}
	}
	close(dfd);
	return true;
}

bool process::set_fds(array<int> fds)
{
	if (fds.size() > INT_MAX) return false;
	
	bool ok = true;
	
	//probably doable with fewer dups, but yawn, implausible.
	for (size_t i=0;i<fds.size();i++)
	{
		while (fds[i] < (int)i && fds[i] >= 0)
		{
			fds[i] = fcntl(fds[i], F_DUPFD_CLOEXEC);
			if (fds[i] < 0) ok = false;
		}
	}
	
	for (size_t i=0;i<fds.size();i++)
	{
		if (fds[i] >= 0 && fds[i] != (int)i)
		{
			if (dup3(fds[i], i, O_CLOEXEC) < 0)
			{
				ok = false;
				close(i);
			}
		}
		if (fds[i] < 0) close(i);
	}
	
	if (!closefrom(fds.size())) ok = false;
	
	return ok;
}


static void update_piperead(int& fd, array<byte>& out, size_t limit)
{
	while (true)
	{
		byte buf[4096];
		ssize_t bytes = ::read(fd, buf, sizeof(buf));
		if (bytes>0) out += arrayview<byte>(buf, bytes);
		if (bytes==0) { close(fd); fd=-1; }
		if (bytes<=0) break;
		if (out.size() > limit) break;
	}
}
void process::update(bool sleep)
{
	if (this->pid != -1)
	{
		//sometimes, I get EOF from the pipes (closed by process teardown), but waitpid() isn't ready to return anything
		//any change to the timing, for example running it under strace, causes this issue to disappear
		//feels like a kernel bug
		//workaround: turn on blocking mode if I suspect that's happening
		//TODO: this can hit if stdout/err are gone due to outlimit
		bool block = (stdout_fd==-1 && stderr_fd==-1);
		if (waitpid(this->pid, &this->exitcode, block ? 0 : WNOHANG) > 0)
		{
			this->pid = -1;
		}
	}
	if (sleep && this->pid != -1)
	{
		fd_set rd;
		fd_set wr;
		FD_ZERO(&rd);
		FD_ZERO(&wr);
		if (stdout_fd != -1) FD_SET(stdout_fd, &rd);
		if (stderr_fd != -1) FD_SET(stderr_fd, &rd);
		if (stdin_fd != -1 && stdin_buf) FD_SET(stdin_fd, &wr);
		select(FD_SETSIZE, &rd, &wr, NULL, NULL);
	}
	
	if (stdin_fd != -1)
	{
		if (stdin_buf)
		{
			ssize_t bytes = ::write(stdin_fd, stdin_buf.ptr(), stdin_buf.size());
			if (bytes == 0)
			{
				stdin_buf.reset();
				close(stdin_fd);
				stdin_fd = -1;
			}
			if (bytes > 0) stdin_buf = stdin_buf.slice(bytes, stdin_buf.size()-bytes);
		}
		if (!stdin_buf && !stdin_open)
		{
			close(stdin_fd);
			stdin_fd = -1;
		}
	}
	
	if (stdout_fd != -1) update_piperead(stdout_fd, stdout_buf, outmax-stderr_buf.size());
	if (stderr_fd != -1) update_piperead(stderr_fd, stderr_buf, outmax-stdout_buf.size());
	
	if (stdout_buf.size()+stderr_buf.size() > outmax)
	{
		if (stdout_fd != -1) close(stdout_fd);
		if (stderr_fd != -1) close(stderr_fd);
		stdout_fd = -1;
		stderr_fd = -1;
	}
}

pid_t process::launch_impl(array<const char*> argv, array<int> stdio_fd)
{
	pid_t ret = fork();
	if (ret == 0)
	{
		//WARNING:
		//fork(), POSIX.1-2008, http://pubs.opengroup.org/onlinepubs/9699919799/functions/fork.html
		//  If a multi-threaded process calls fork(), the new process shall contain a replica of the
		//  calling thread and its entire address space, possibly including the states of mutexes and
		//  other resources. Consequently, to avoid errors, the child process may only execute
		//  async-signal-safe operations until such time as one of the exec functions is called.
		//In particular, malloc must be avoided.
		
		set_fds(std::move(stdio_fd));
		for (int i=0;i<=2;i++) fcntl(i, F_SETFD, 0); // remove FD_CLOEXEC
		execvp(argv[0], (char**)argv.ptr());
		while (true) _exit(EXIT_FAILURE);
	}
	
	return ret;
}

bool process::launch(cstring prog, arrayview<string> args)
{
	array<const char*> argv;
	argv.append((const char*)prog);
	for (size_t i=0;i<args.size();i++)
	{
		argv.append((const char*)args[i]);
	}
	argv.append(NULL);
	
	int stdinpair[2];
	int stdoutpair[2];
	int stderrpair[2];
	//leaks a couple of fds if creation fails, but if that happens, process is probably screwed anyways.
	if (pipe2(stdinpair,  O_CLOEXEC) < 0) return false; // cloexec is removed in launch_impl
	if (pipe2(stdoutpair, O_CLOEXEC) < 0) return false;
	if (pipe2(stderrpair, O_CLOEXEC) < 0) return false;
	
	int stdio_fd[3] = { stdinpair[0], stdoutpair[1], (this->stderr_split ? stderrpair[1] : stdoutpair[1]) };
	//array<int> stdio_fd;
	//stdio_fd.append(stdinpair[0]); // if stdin isn't desired, the other end is closed by the update() at the end
	//stdio_fd.append(stdinpair[1]);
	//stdio_fd.append(this->stderr_split ? stderrpair[1] : stdoutpair[1]);
	this->pid = launch_impl(std::move(argv), arrayview<int>(stdio_fd));
	
	close(stdinpair[0]);
	close(stdoutpair[1]);
	close(stderrpair[1]);
	this->stdin_fd = stdinpair[1];
	this->stdout_fd = stdoutpair[0];
	this->stderr_fd = stderrpair[0];
	fcntl(this->stdin_fd, F_SETFL, fcntl(this->stdin_fd, F_GETFL)|O_NONBLOCK);
	fcntl(this->stdout_fd, F_SETFL, fcntl(this->stdout_fd, F_GETFL)|O_NONBLOCK);
	fcntl(this->stderr_fd, F_SETFL, fcntl(this->stderr_fd, F_GETFL)|O_NONBLOCK);
	
	if (this->pid < 0)
	{
		destruct();
		return false;
	}
	
	update();
	return true;
}

bool process::running(int* exitcode)
{
	update();
	if (exitcode && this->pid==-1) *exitcode = this->exitcode;
	return (this->pid != -1);
}

void process::wait(int* exitcode)
{
	this->stdin_open = false;
	while (this->pid != -1) update(true);
	if (exitcode) *exitcode = this->exitcode;
}

void process::terminate()
{
	if (this->pid != -1)
	{
		kill(this->pid, SIGKILL);
		waitpid(this->pid, NULL, 0);
		this->exitcode = -1;
		this->pid = -1;
	}
}

void process::destruct()
{
	terminate();
	if (stdin_fd!=-1) close(stdin_fd);
	if (stdout_fd!=-1) close(stdout_fd);
	if (stderr_fd!=-1) close(stderr_fd);
}

process::~process()
{
	destruct();
}
#endif

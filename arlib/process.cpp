#include "process.h"
#include "test.h"

#include <spawn.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <signal.h>

//WARNING: fork() only clones the calling thread. Other threads could be holding important
// resources, such as the malloc mutex.
//As such, post-fork() code must only call async-signal-safe functions. In particular, be careful with C++ destructors.

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
//throws about fifty errors in Valgrind - don't care
static bool fd_closeall(arrayview<int> fd_keep)
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
//printf("(%i)",off);fflush(stdout);
			linux_dirent64* dent = (linux_dirent64*)(bytes+off);
			off += dent->d_reclen;
			
			int fd = atoi_simple(dent->d_name);
			if (fd>=0 && fd!=dfd && !fd_keep.contains(fd))
			{
//printf("(x=%i)",fd);fflush(stdout);
				close(fd);
			}
		}
	}
	close(dfd);
	return true;
}

class process : nocopy {
	pid_t pid = -1;
	int exitcode = -1;
	
	bool stdin_open = false;
	array<byte> stdin_buf;
	int stdin_fd = -1;
	
	int stdout_fd = -1;
	array<byte> stdout_buf;
	
	bool stderr_split = false;
	int stderr_fd = -1;
	array<byte> stderr_buf;
	
	void update(bool sleep = false)
	{
		if (this->pid != -1)
		{
			//sometimes, I get EOF from the pipes (closed by process teardown), but waitpid() isn't ready to return anything
			//any change to the timing, for example running it under strace, causes this issue to disappear
			//feels like a kernel bug
			//workaround: turn on blocking mode if I suspect that's happening
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
				ssize_t bytes = write(stdin_fd, stdin_buf.ptr(), stdin_buf.size());
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
		
		if (stdout_fd != -1)
		{
			while (true)
			{
				byte buf[4096];
				ssize_t bytes = read(stdout_fd, buf, sizeof(buf));
				if (bytes>0) stdout_buf += arrayview<byte>(buf, bytes);
				if (bytes==0) { close(stdout_fd); stdout_fd=-1; }
				if (bytes<=0) break;
			}
		}
		
		if (stderr_fd != -1)
		{
			while (true)
			{
				byte buf[4096];
				ssize_t bytes = read(stderr_fd, buf, sizeof(buf));
				if (bytes>0) stderr_buf += arrayview<byte>(buf, bytes);
				if (bytes==0) { close(stderr_fd); stderr_fd=-1; }
				if (bytes<=0) break;
			}
		}
	}
	
public:
	process() {}
	process(cstring path, arrayview<string> args) { launch(path, args); }
	
	bool launch(cstring path, arrayview<string> args)
	{
		array<const char*> argv;
		argv.append((char*)path.bytes().ptr());
		for (size_t i=0;i<args.size();i++)
		{
			argv.append((char*)args[i].bytes().ptr());
		}
		argv.append(NULL);
		
		int stdinpair[2];
		int stdoutpair[2];
		int stderrpair[2];
		pipe2(stdinpair, O_CLOEXEC);  // close-on-exec for a fd intended only to be used by a child process?
		pipe2(stdoutpair, O_CLOEXEC); // answer: dup2 resets cloexec
		pipe2(stderrpair, O_CLOEXEC); // O_NONBLOCK is absent because no child expects a nonblocking stdout
		
		this->pid = fork();
		if (this->pid == 0)
		{
			//the following external functions are used in the child process:
			//dup2, open, close, execvp, _exit - documented as sigsafe
			//syscall(getdents64) - all syscalls (except maybe some signal/etc-related ones) are sigsafe
			//ptrace - thin wrapper around a syscall
			//array::ptr and a couple of arrayview members - memory only
			
			dup2(stdinpair[0], 0);
			dup2(stdoutpair[1], 1);
			if (this->stderr_split) dup2(stderrpair[1], 2);
			else dup2(stdoutpair[1], 2);
			
			int keep[] = {0,1,2};
			fd_closeall(keep); // calls open, syscall(getdents64), close
			
			//for sandboxing
			//ptrace(PTRACE_TRACEME);
			
			execvp(path, (char**)argv.ptr());
			while (true) _exit(EXIT_FAILURE);
		};
		
		close(stdinpair[0]);
		close(stdoutpair[1]);
		close(stderrpair[1]);
		this->stdin_fd = stdinpair[1];
		this->stdout_fd = stdoutpair[0];
		this->stderr_fd = stderrpair[0];
		fcntl(this->stdin_fd, F_SETFL, fcntl(this->stdin_fd, F_GETFL)|O_NONBLOCK);
		fcntl(this->stdout_fd, F_SETFL, fcntl(this->stdout_fd, F_GETFL)|O_NONBLOCK);
		fcntl(this->stderr_fd, F_SETFL, fcntl(this->stderr_fd, F_GETFL)|O_NONBLOCK);
		
		if (this->pid == -1)
		{
			destruct();
			return false;
		}
		
		update();
		return true;
	}
	
	template<typename... Args>
	bool launch(cstring path, Args... args)
	{
		string argv[sizeof...(Args)] = { args... };
		return launch(path, arrayview<string>(argv));
	}
	
	bool launch(cstring path)
	{
		return launch(path, arrayview<string>(NULL));
	}
	
	//Sets the child's stdin. If called multiple times, they're concatenated.
	void stdin(arrayview<byte> data) { stdin_buf += data; update(); }
	void stdin(cstring data) { stdin(data.bytes()); }
	//If interact(true) is called before launch(), stdin() can be called after process start.
	//To close the child's stdin, call interact(false).
	void interact(bool enable)
	{
		this->stdin_open = enable;
		update();
	}
	
	//If the process is still running, these contain what it's written thus far. The data is discarded after being read.
	//The default is to merge stderr into stdout. To keep them separate, call stderr() before launch().
	//If wait is true, the functions will wait until the child exits or prints something on the relevant stream.
	array<byte> stdoutb(bool wait = false)
	{
		update(false);
		while (wait && !stdout_buf) update(true);
		return std::move(stdout_buf);
	}
	array<byte> stderrb(bool wait = false)
	{
		stderr_split=true;
		update(false);
		while (wait && !stderr_buf) update(true);
		return std::move(stderr_buf);
	}
	//These two can return invalid UTF-8. Even if the program is specified to only process UTF-8, it's possible to read half a character.
	string stdout(bool wait = false) { return string(stdoutb(wait)); }
	string stderr(bool wait = false) { return string(stderrb(wait)); }
	
	bool running(int* exitcode = NULL)
	{
		update();
		if (exitcode && this->pid==-1) *exitcode = this->exitcode;
		return (this->pid != -1);
	}
	
	void wait(int* exitcode = NULL)
	{
		this->stdin_open = false;
		while (this->pid != -1) update(true);
		if (exitcode) *exitcode = this->exitcode;
	}
	
	void terminate()
	{
		if (this->pid != -1)
		{
			kill(this->pid, SIGKILL);
			this->exitcode = -1;
			this->pid = -1;
		}
	}
	
private:
	void destruct()
	{
		terminate();
		if (stdin_fd!=-1) close(stdin_fd);
		if (stdout_fd!=-1) close(stdout_fd);
		if (stderr_fd!=-1) close(stderr_fd);
	}
public:
	
	~process()
	{
		destruct();
	}
};

#ifdef __linux__
test()
{
	//there are a couple of race conditions here, but I believe they're all safe
	{
		process p;
		assert(p.launch("/bin/true"));
		int status;
		p.wait(&status);
		assert_eq(status, 0);
	}
	
	{
		process p;
		assert(p.launch("/bin/echo", "foo"));
		int status;
		p.wait(&status);
		assert_eq(status, 0);
		assert_eq(p.stdout(), "foo\n");
	}
	
	{
		process p;
		p.stdin("foo");
		assert(p.launch("/bin/cat"));
		p.wait();
		assert_eq(p.stdout(), "foo");
	}
	
	{
		process p;
		p.interact(true);
		assert(p.launch("/bin/cat"));
		p.stdin("foo");
		p.wait();
		assert_eq(p.stdout(), "foo");
	}
	
	{
		process p;
		p.interact(true);
		assert(p.launch("/bin/cat"));
		p.stdin("foo");
		usleep(1*1000);
		assert_eq(p.stdout(), "foo");
		assert(p.running());
		p.interact(false);
		usleep(1*1000); // this gets interrupted by SIGCHLD, but it's resumed
		assert(!p.running());
	}
	
	{
		process p;
		assert(p.launch("/bin/echo", "foo"));
		assert_eq(p.stdout(), "");
		assert_eq(p.stdout(true), "foo\n");
	}
}
#else
#error find windows equivalents
#endif

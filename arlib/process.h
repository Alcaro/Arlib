#pragma once
#include "global.h"
#include "string.h"
#include "thread.h"

//Be careful about creating child processes through other functions. Make sure they don't fight over any process-global resources.
//Said resources are waitpid(-1) and SIGCHLD. This one requires the latter, and requires that nothing uses the former.
//g_spawn_*(), popen() and system() are safe. However, g_child_watch_*() is not.
class process : nocopy {
//Linux has none of these syscalls:
//- Await any one of these processes, but not others (like select() awaits some, but not all, file descriptors)
//- Await any child process, but don't close the process handle
//- Await any child process, with timeout (WNOHANG doesn't count, it doesn't support nonzero timeouts)
//so there's no real way to handle multiple children without hijacking something process-global.
//clonefd would implement #1 and #3 (#2 isn't really needed), but only exists in Capsicum. And it seems incompatible with ptrace.
//Strange how such an ancient limitation has lived so long. Windows has had WaitForMultipleObjects since approximately forever.
//An alternative solution would be chaining SIGCHLD handlers, but no userspace supports that. Except, again, it works fine on Windows.

//'Await child process, but not threads' seems to be __WNOTHREAD, or maybe that's the default? Thread/process/thread group confuses me.
//__WNOTHREAD may be accepted only on wait4, not waitid?

//We can't cooperate with glib, g_child_watch doesn't propagate ptrace events.
//But we can ignore it. Only g_child_watch touches the SIGCHLD handler, so we can safely ignore that one and claim it for ourselves.

//However, there is a workaround: Await the processes' IO handles, instead of the process itself.
// They die with the process, can be bulk awaited, waiting changes no state, and there are timeouts.
//Except there are still problems: Both the child and outlimit can close stdout.
// But that can be solved too: Await some fourth file descriptor. Few processes care about unknown file descriptors.
//  closefrom() variants do, but that's rare enough to ignore.
//   And if the process is unexpectedly still alive when its control socket dies, easy fix.
//  For the sandbox, this can be the SCM_RIGHTS pipe. For anything else, create a dummy pipe().
//   Or use the three standard descriptors. Exceeding outlimit gives SIGPIPE, closing stdout prior to exit is a negligible probability,
//     and we can add a timeout to select() and randomly waitpid them.
//But the easiest solution remains: Claim SIGCHLD for ourselves.

	//todo: use fd monitor instead
	void update(bool sleep = false);
	bool stdin_open = false;
	array<byte> stdin_buf;
	array<byte> stdout_buf;
	bool stderr_split = false;
	array<byte> stderr_buf;
	size_t outmax = SIZE_MAX;
	
#ifdef __linux__
	int stdin_fd = -1;
	int stdout_fd = -1;
	int stderr_fd = -1;
	
	void destruct();
	
	//todo: rewrite for sandbox support
protected:
	static void manager();
	
	pid_t pid = -1;
	int exitcode = -1;
	
	mutex mut;
	
	static bool closefrom(int lowfd);
	//Sets the file descriptor table to fds, closing all existing fds.
	//If a fd is -1, it's closed. Duplicates in the input are allowed.
	//Returns false on failure, but keeps doing its best anyways.
	//Afterwards, all new fds have the cloexec flag set. If this is undesired, use fcntl(F_SETFD).
	static bool set_fds(array<int> fds);
	
	//Like execlp, this searches PATH for the given program.
	static string find_prog(const char * prog);
	
	//stdio_fd is an array of { stdin, stdout, stderr } and should be sent to set_fds (possibly with a few additions) post-fork.
	//Must return the child's pid, or -1 on failure.
	virtual pid_t launch_impl(array<const char*> argv, array<int> stdio_fd);
#endif
	
#ifdef _WIN32
	HANDLE proc = NULL;
	int exitcode = -1;
	
	HANDLE stdin_h = NULL;
	HANDLE stdout_h = NULL;
	HANDLE stderr_h = NULL;
#endif
	
public:
	process() {}
	process(cstring prog, arrayview<string> args) { launch(prog, args); }
	
	//Argument quoting is fairly screwy on Windows. Command line arguments at all are fairly screwy on Windows.
	//You may get weird results if you use too many backslashes, quotes and spaces.
	bool launch(cstring prog, arrayview<string> args);
	
	template<typename... Args>
	bool launch(cstring prog, Args... args)
	{
		string argv[sizeof...(Args)] = { args... };
		return launch(prog, arrayview<string>(argv));
	}
	
	bool launch(cstring prog)
	{
		return launch(prog, arrayview<string>(NULL));
	}
	
	//TODO:
	//allow setting stdin to any of inherit, pipe, file, buffer
	//allow setting stdout to any of inherit, pipe, file, discard
	//allow setting stdout to any of inherit, pipe, file, discard, stdout
	//
	//what to name them? can't use stdin or in, they're macros on msvc
	//stdin_inherit()/etc? that's 13 functions just for modesetting
	//set_stdin(process::stdin)? ugly namespacing
	//input.inherit()? seems hard to allow linking stderr to stdout in that model
	//input = STDIN_FILENO? probably too linux specific
	//input = process::input::create_inherit()? kinda bulky, but not too terrible
	//set_stdin(process::input::create_inherit())? even bulkier, but public members tend to be screwy
	//set_stdin(pipe::create_stdin())? probably good, except need directionally unambiguous typenames
	//piperead/pipewrite? who writes? inpipe? whose input?
	//procin? a bit too cramped
	//process::input is probably my best bet, clear enough that it's process input aka caller calls write()
	
	class input : nocopy {
		input() {}
		
		uintptr_t fd; // HANDLE on windows, int on linux
		friend class process;
		
		array<byte> buf;
		bool do_close = false;
		
		void update(int fd);
		
	public:
		void write(arrayview<byte> data) { buf += data; update(fd); }
		void write(cstring data) { write(data.bytes()); }
		//Sends EOF to the child, after all bytes have been written.
		void close() { do_close = true; update(fd); }
		
		static input* create_pipe(arrayview<byte> data = NULL);
		// Like create_pipe, but auto closes the pipe once everything has been written.
		static input* create_buffer(arrayview<byte> data = NULL);
		
		//Can't use write/close on these two. Just don't store them anywhere.
		static input* create_file(cstring path);
		//Uses caller's stdin. Make sure no two processes are trying to use stdin simultaneously, it acts badly.
		static input* create_stdin();
	};
	//The process object takes ownership of the given object.
	//Can only be called before launch(), and only once.
	//Default is NULL, equivalent to /dev/null.
	void set_stdin(input* inp);
	
	class output : nocopy {
		uintptr_t fd;
		friend class process;
		
		array<byte> buf;
		size_t maxbytes = SIZE_MAX;
		
		void update(int fd);
		
	public:
		//Stops the process from writing too much data and wasting RAM.
		//If there, at any point, is more than 'max' bytes of unread data in the buffer, the pipe is closed.
		//Slightly more may be readable in practice, due to kernel-level buffering.
		void limit(size_t lim) { maxbytes = lim; }
		
		array<byte> readb();
		//{
		//	update(fd);
		//	return std::move(buf);
		//}
		//Can return invalid UTF-8. Even if the program only processes UTF-8, it's possible
		// to read half a character, if the process is still running or the outlimit was hit.
		string read() { return string(readb()); }

		static output* create_pipe();
		static output* create_buffer();
		static output* create_file(cstring path, bool append = false);
		static output* create_stdout();
		static output* create_stderr();
		
		virtual ~output() {}
	};
	void set_stdout(output* outp);
	void set_stderr(output* outp);
	
	
	
	
	//Sets the child's stdin. If called multiple times, they're concatenated.
	void write(arrayview<byte> data) { stdin_buf += data; update(); }
	void write(cstring data) { write(data.bytes()); }
	
	//If interact(true) is called before launch(), write() can be called after process start.
	//To close the child's stdin, call interact(false).
	void interact(bool enable)
	{
		this->stdin_open = enable;
		update();
	}
	
	//Stops the process from writing too much data and wasting RAM.
	//If there, at any point, is more than 'max' bytes of unread data in the buffers, the stdout/stderr pipes are closed.
	//Slightly more may be readable in practice, due to kernel-level buffering.
	void outlimit(size_t max) { this->outmax = max; }
	
	//Returns what the process has written thus far (if the process has exited, all of it). The data is discarded after being read.
	//The default is to merge stderr into stdout. To keep them separate, call error() before launch().
	//If wait is true, the functions will wait until the child exits or prints something on the relevant stream.
	array<byte> readb(bool wait = false)
	{
		update(false);
		while (wait && !stdout_buf) update(true);
		return std::move(stdout_buf);
	}
	array<byte> errorb(bool wait = false)
	{
		stderr_split=true;
		update(false);
		while (wait && !stderr_buf) update(true);
		return std::move(stderr_buf);
	}
	//These two can return invalid UTF-8. Even if the program only processes UTF-8, it's possible to read half a character.
	string read(bool wait = false) { return string(readb(wait)); }
	string error(bool wait = false) { return string(errorb(wait)); }
	//TODO: Allow stdout/stderr to follow the parent
	
	bool running(int* exitcode = NULL);
	void wait(int* exitcode = NULL);
	void terminate(); // The process is automatically terminated if the object is destroyed.
	
	~process();
};

#pragma once
#include "global.h"
#include "string.h"

class process : nocopy {
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
	
protected:
	pid_t pid = -1;
	int exitcode = -1;
	
	//WARNING: fork() only clones the calling thread. Other threads could be holding important
	// resources, such as the malloc mutex.
	//As such, the preexec callback must only call async-signal-safe functions.
	//These are the direct syscall wrappers, plus those listed at <http://man7.org/linux/man-pages/man7/signal.7.html>.
	//In particular, be careful with C++ destructors.
	struct execparm {
		int nfds_keep; // After preexec(), all file descriptors >= this (default 3) will be closed.
		const char * const * environ; // If non-NULL, the child will use that environment. Otherwise, inherited from parent.
		                              // Remember that you can't call malloc, so set this up before calling this.
	};
	function<void(execparm* params)> preexec;
	
	//virtual void waitpid_select(bool sleep);
	virtual bool launch_impl(cstring path, arrayview<string> args);
	
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
	process(cstring path, arrayview<string> args) { launch(path, args); }
	
	//Argument quoting is fairly screwy on Windows. Command line arguments at all are fairly screwy on Windows.
	//You may get weird results if you use too many backslashes, quotes and spaces.
#ifdef __linux__
	bool launch(cstring path, arrayview<string> args) { return launch_impl(path, args); }
#else
	bool launch(cstring path, arrayview<string> args);
#endif
	
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
	//The default is to merge stderr into stdout. To keep them separate, call stderr() before launch().
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
	
	bool running(int* exitcode = NULL);
	void wait(int* exitcode = NULL);
	void terminate(); // The process is automatically terminated if the object is destroyed.
	
	~process();
};

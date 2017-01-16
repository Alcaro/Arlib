#pragma once
#include "global.h"
#include "string.h"

class process : nocopy {
#ifdef __linux__
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
	
	void update(bool sleep = false);
	
	void destruct();
	
protected:
	//WARNING: fork() only clones the calling thread. Other threads could be holding important
	// resources, such as the malloc mutex.
	//As such, the preexec callback must only call async-signal-safe functions.
	//These are the direct syscall wrappers, plus those listed at <http://man7.org/linux/man-pages/man7/signal.7.html>.
	//In particular, be careful with C++ destructors.
	//No file descriptors are open during preexec(), except stdin/stdout/stderr which are piped to foo.
	bool launch_with_preexec(cstring path, arrayview<string> args, function<void()> preexec);
#endif
public:
	process() {}
	process(cstring path, arrayview<string> args) { launch(path, args); }
	
#ifdef __linux__
	virtual // for the sandbox
#endif
	bool launch(cstring path, arrayview<string> args);
	
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
	void stdin(arrayview<byte> data);
	void stdin(cstring data) { stdin(data.bytes()); }
	
	//If interact(true) is called before launch(), stdin() can be called after process start.
	//To close the child's stdin, call interact(false).
	void interact(bool enable);
	
	//Returns what the process has written thus far (if the process has exited, all of it). The data is discarded after being read.
	//The default is to merge stderr into stdout. To keep them separate, call stderr() before launch().
	//If wait is true, the functions will wait until the child exits or prints something on the relevant stream.
	array<byte> stdoutb(bool wait = false);
	array<byte> stderrb(bool wait = false);
	//These two can return invalid UTF-8. Even if the program only processes UTF-8, it's possible to read half a character.
	string stdout(bool wait = false) { return string(stdoutb(wait)); }
	string stderr(bool wait = false) { return string(stderrb(wait)); }
	//TODO: test stderr
	
	bool running(int* exitcode = NULL);
	void wait(int* exitcode = NULL);
	void terminate();
	
	~process();
};
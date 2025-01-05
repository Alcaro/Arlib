#pragma once
#include "file.h"
#include "runloop2.h"

class process {
	fd_t fd;
	int m_pid = 0;
public:
	
	struct raw_params {
		const char * prog = NULL;
		const char * const * argv = NULL;
		const char * const * envp = NULL;
		arrayvieww<fd_raw_t> fds; // Will be mutated.
		bool detach = false;
	};
	struct params {
		string prog; // If this doesn't contain a slash, find_prog will be called. If it's empty, argv[0] will be used.
		array<string> argv; // If this is empty, prog will be used.
		array<string> envp; // If this is empty, the program's environment will be used.
		array<fd_raw_t> fds; // Will be mutated. If shorter than three elements, will be extended with 0 1 2. -1 will be replaced with fd_t::create_devnull(); -2 will be closed.
		bool detach = false;
	};
	// todo: delete this when all callers have been audited to expect bool and not int
	class strict_bool {
		bool val;
	public:
		strict_bool(bool val) : val(val) {}
		explicit operator bool() { return val; }
	};
	// Returns the child process' ID, or zero for failure.
	// If detached, will always return zero, and the process object will contain nothing.
	// todo(kernel 6.13): add a 
	// I'd add a get_pid() function if I could, and make this return bool,
	//  but the only way to get a pid from a pidfd is reading /proc/self/fdinfo/123.
	// This will change in kernel 6.13 (to be released january 2025), which adds ioctl(PIDFD_GET_INFO).
	strict_bool create(raw_params& param);
	strict_bool create(params& param);
	strict_bool create(raw_params&& param) { return create((raw_params&)param); }
	strict_bool create(params&& param) { return create((params&)param); }
	
	async<int> wait(); // Only the first await works; subsequent ones will just return -1.
	void terminate();
	int pid() { return m_pid; } // todo(kernel 6.13): change to ioctl(PIDFD_GET_INFO)
	
	~process() { terminate(); }
	
	class pipe {
		pipe() = delete;
		pipe(fd_t rd, fd_t wr) : rd(std::move(rd)), wr(std::move(wr)) {}
		pipe(const pipe&) = delete;
		
	public:
		static pipe create();
		pipe(pipe&& other) = default;
		
		fd_t rd;
		fd_t wr;
	};
	
	// If argument contains a slash, returns it unchanged. If not, splits PATH by colon and returns the first that exists.
	static string find_prog(cstring prog);
	
	// The below functions should only be used by process-like objects that want to launch the process in a special way, such as Gravelcube.
	// They should not be used by any normal program.
	
	// Sets the file descriptor table to 'fds', closing all other fds.
	// If an entry is negative, the corresponding fd is closed. (Note that this does not match params::fds.)
	// Duplicates in the input are allowed. Returns false on failure, but keeps doing its best anyways.
	// Will mangle the input array. While suboptimal, it's the only way to avoid a post-fork malloc.
	// The CLOEXEC flag is set to 'cloexec' on all remaining fds.
	static bool set_fds(arrayvieww<int> fds, bool cloexec = false);
	
	static bool process_try_wait(fd_t& fd, int& ret, bool async);
	static int process_wait_sync(fd_t& fd);
};

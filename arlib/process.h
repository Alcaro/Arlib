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
//Strange how such an ancient limitation has lived so long. Windows has had WaitForMultipleObjects since at least XP.
//An alternative solution would be chaining SIGCHLD handlers, but no userspace supports that. Except, again, it works fine on Windows.

//'Await child process, but not threads' seems to be __WNOTHREAD, or maybe that's the default? Thread/process/thread group confuses me.
//__WNOTHREAD may be accepted only on wait4, not waitid?

//We can't cooperate with glib, g_child_watch doesn't propagate ptrace events.
//But we can ignore it. Only g_child_watch touches the SIGCHLD handler, so we can safely claim it for ourselves.

//However, there is a workaround: Await the processes' IO handles, instead of the process itself.
// They die with the process, can be bulk awaited, waiting changes no state, and there are timeouts.
//Except there are still problems: Both the child and outlimit can close stdout.
// But that can be solved too: Await some fourth file descriptor. Few processes care about unknown file descriptors.
//  closefrom() variants do, but that's rare enough to ignore.
//   And if the process is unexpectedly still alive when its control socket dies, easy fix.
//  For the sandbox, this can be the SCM_RIGHTS pipe. For anything else, create a dummy pipe().
//   Or use the three standard descriptors. Exceeding outlimit gives SIGPIPE, closing stdout is a neglible probability,
//     and we can add a timeout to select() and randomly waitpid them.
//    Or SIGCHLD.

//Except ptrace is still waitpid, not a normal fd.
// But that too can be worked around: Don't use ptrace. Use LD_PRELOAD to reroute open()/etc.
//  Except there's still a bootstrap issue: execve() and open(env LD_PRELOAD) are required, but can't be allowed.
//   execve can be execveat, which can be whitelisted in seccomp, but open() can't be hacked like that.
//   Instead, I can do the ptrace parts synchronously until the LD_PRELOAD library is active, then detach.
//    Of course, I need timeouts on this sync part, in case the program doesn't do the expected syscalls.
//     There is no timeout on waitpid, but setitimer(ITIMER_REAL, 10ms, NULL) would yield EINTR.
//      Unless it restarts. Or the signal ends up on another thread. Or two threads do this at once. Or something even weirder.
//       The only solution is SIGCHLD.
//    Since ptrace breaks at every syscall, and I'd kill the child if it does anything unexpected, maybe I can enable BPF in LD_PRELOAD?
//     No, I can't. It'd require auditing every syscall argument (I can't assume a sane INTERP), including the BPF filter itself,
//       and that's harder than just locking down early.
//  Stuff doing raw syscalls in the main program would still break, of course, but that's so unlikely it can be ignored.
//   Except libc itself.
//    But that can be handled via SECCOMP_RET_TRAP / SIGSYS.
//     open(env LD_PRELOAD) still needs to be handled. It I trap it, I can't handle it in ptrace; if I ptrace it, I can't trap it post-boot.
//      Solution 1: Add another BPF filter, changing open() from RET_TRACE to RET_TRAP.
//      Solution 2: Open the preload library pre-exec, let it trap, consume the signal, and "return" the already-open file.
// INTERP is processed by the kernel and not visible to ptrace/etc, so I can't restrict it. Can I leak anything with a hacked INTERP?
//  Probably not. A leakable program must:
//   - Not use .got.plt, it's filled in by INTERP
//       BusyBox matches this criteria; copy /bin/true to ./ls, set INTERP to BusyBox, execute, get directory listing
//   - Be confidental; leaking a distro-provided file is harmless
//   - Be at a known path; this must be some kind of leak or bruteforce, any predictable path is distro-provided
//   and that combination is neglible. #1 alone is impossible for any plausible program.
//   Still, it is a worrying thought.
//  Maybe I can restrict INTERP with chroot, but I don't think that's available without root.
//   Or maybe namespaces?
//    Or explicitly call the correct INTERP.
//     (BusyBox segfaults if I do this.)
//     Actually, that could probably avoid ptrace too.
//      Except it'd require open(/proc/self/fd/4), or a fail-open seccomp if LD_PRELOAD installation fails. ptrace still needed.


//Process manager thread:
//- installs SIGCHLD handler, which writes a byte to an internal pipe
//    SIGCHLD from unfamiliar processes (like ptrace setup) is fine, they do nothing when pipe is read
//    SIGCHLD isn't really needed, nearly all process deaths are accompanied with a fd dying, but 100% > 99.99%
//    make sure to preserve errno
//- select()s all open stdout/stderr/sandbox pipes (plus stdin, if applicable), plus the SIGCHLD pipe
//- any activity is dispatched to the relevant handler
//    use the synchronized(){} macro in said handlers
//    sandbox:
//      if it can't write, child is clearly misbehaving, so kill it rather than awaiting writability
//      flatten all ., .. and extra /s
//        can't protect against symlink shenanigans, O_NOFOLLOW does a little but doesn't help if a dir symlink exists
//          do not under any circumstances allow the child access to symlinks
//          O_BENEATH would help, but that's Capsicum only, despite its uses for avoiding .. attacks on web servers
//            though web servers probably want symlinks? but on the other hand, locking .. is easy if you think about it
//    SIGCHLD pipe data is ignored, it just wakes the select()
//- if any process has no fds (stdin, stdout, stderr, sandbox), waitpid it and see if it died; if yes, remove from the process list
//    if process has fds, no point waiting, fds die when process does and awaiting a dead pipe is activity
//(I could probably use g_child_watch here, but no point.)

//Sandbox bootstrap:
//- set fd 3 to AF_UNIX socket pair (created pre-fork)
//- set fd 4 to file containing preload library (possibly memfd; could create it post-fork, but pre-fork allows sharing it)
//- PTRACE_TRACEME
//- enable seccomp
//- exec LD_PRELOAD=/!/arlib-sandbox.so /lib/ld-linux.so app.elf
//- parent ptrace awaits the SECCOMP_RET_TRAP SIGSYS from open(LD_PRELOAD)
//- ptrace fakes a return of 4 and detaches
//    if it gets anything else, or hits a 10ms timeout, kill child
//      timeout is implemented via SIGCHLD
//        or WNOHANG and some 1ms sleeps, if I can't get SIGCHLD working
//- preload lib overrides open(), replacing it with send message to parent and get fd via SCM_RIGHTS (or an error), see below
//- it also installs a SIGSYS handler, which does the same thing
//- and closes fd 4, for good measure (it's mapped anyways)
//- main app enters, not knowing what's going on
//    until it uses sandcomm::connect(), which is implemented by checking for the LD_PRELOAD value
//      how does sandcomm find the preload lib? does dlopen(/!/arlib-sandbox.so) work, or do I 'extend' some syscall?

//Sandbox socket protocol:
//struct {
//  int msgtype { open=1, sandcomm=2 };
//  int errno;
//};
//fd sent separately

//LD_PRELOAD operation:
//  global mutex around the function, same mutex for open() and sandcomm (multi-threaded open() probably isn't the bottleneck)
//  if open(), sendmsg()
//    if that fails, sleep (select()) and retry
//  if sandcomm, check if the queue (8 ints, fd or -1) contains anything; if yes, return that
//  recvmsg()
//    if that fails, abort()
//  if relevant, return that
//  otherwise, add to sandcomm queue (incorrect open() replies are impossible), repeat recvmsg

//Likely traps in the sandbox:
//- symlink(): make sure both linkpath and target are allowed, and target is writable if linkpath is
//- exec*(): opening INTERP is not visible to BPF, and can't be audited without TOCTTOU
//    instead, this must be forcibly prefixed with /lib/ld-linux.so, which is a fairly annoying dance:
//    - get ptraced (maybe never detach at all?)
//    - tracer suspends all threads (unless ptrace does that automatically)
//    - inspect arguments
//        ensure both argv and argv[0] are in anonymous, non-shared memory
//          both start and end, page-straddling tastes bad
//            could ensure argv and argv[0] are both in the same memory page, makes things easier
//              but verification is still needed
//                I don't know how to do that
//                  I can't cheese it by demanding it's in LD_PRELOAD's BSS, the library could be unmapped and something else put there
//        test that host properly handles the memory being non-readable (kill child)
//    - approve syscall
//    - on failure, unsuspend threads (on success, exec kills all other threads)
//    - detach (maybe)
//- fork():
//    requires a new AF_UNIX pipe
//      which of the parent's data structures get screwed up by that?
//    possibly strange interactions with ptrace; it works, strace does it, but at what cost?
//- OOM killer, can it be tricked into killing something it shouldn't?

//so much special cases. BPF covers 95% of what I need, but the last 5% are all over the place, and there are no obvious ways to block them.
//Nearly all of it is due to lack of kernel-level file system lockdown. (Child process handling is tricky, but not impossible.)
//  Namespaces and chroot can do it, but that requires setuid, which gives root on failure.
//    That is the ultimate irony and cannot be tolerated, or even risked.
//It's better than Windows' hodgepodge of forty things locking 2% each, but for a sandbox, anything short of perfect is quite literally zero.
//Maybe I should entrust this entire thing to Firejail. It's still setuid, but at least failures there wouldn't be my fault.

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
	static void manager();
	
	pid_t pid = -1;
	int exitcode = -1;
	
	mutex mut;
	
	//WARNING: fork() only clones the calling thread. Other threads could be holding important
	// resources, such as the malloc mutex.
	//As such, the preexec callback must only call async-signal-safe functions.
	//These are the direct syscall wrappers, plus those listed at <http://man7.org/linux/man-pages/man7/signal.7.html>.
	//In particular, be careful with C++ destructors.
	struct execparm {
		int nfds_keep; // After preexec(), all file descriptors >= this (default 3) will be closed.
		const char * const * environ; // If non-NULL, the child will use that environment. Otherwise, inherited from parent.
		                              // Remember that you can't call malloc, so set this up before launch() is called.
	};
	//For pre-fork and post-fork, override launch_impl.
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

#include "../process.h"

/*
//Allows safely executing untrusted code.
//
//Exact rules:
// Other than as allowed by the parent, the child may not read per-user data or system configuration,
//  or write to permanent storage. Internet counts as permanent. The child may find out what OS it's running on.

//Not implemented on Windows. It provides plenty of ways to restrict a process, but
//- All I could find are blacklists, disabling a particular privilege; I want whitelists
//- There are so many resource kinds I can't keep track how to restrict everything, or even list
//    them; it will also fail-open if a new resource kind is introduced
//- Many lockdown functions temporarily disable privileges, rather than completely delete them
//- There's little or no documentation on which privileges are required for the operations I need
//- The lockdown functions are often annoying to call, involving variable-width arrays in
//    structures, and LCIDs that likely vary between reboots
//I cannot trust such a system. I only trust whitelists, like Linux seccomp.
//Even Chrome couldn't find anything comprehensive; they use everything they can find (restricted token, job object, desktop,
// SetProcessMitigationPolicy, firewall, etc), but some operations, such as accessing FAT32 volumes, still pass through.
//It feels like the Windows sandboxing functions are designed for trusted code operating on untrusted data, rather than untrusted code.
//Since I can't create satisfactory results in such an environment, I won't even try.

#ifdef __linux__
//Currently, both parent and child must be x86_64.

class sandcomm;
class sandproc : public process {
	sandcomm* conn;
	
	pthread_t ptrace_thr;
	void ptrace_func();
	static void* ptrace_func_c(void* arg);
	
	void preexec_fn(execparm* params);
	bool launch_impl(cstring path, arrayview<string> args) override;
	//void waitpid_select(bool sleep) override;
	
public:
	sandproc() : conn(NULL) {}
	
	//If the child process uses Arlib, this allows convenient communication with it.
	//Must be called before starting the child, and exactly once (or never).
	//Like process, the object is not thread safe.
	sandcomm* connect();
	
	//Only available before starting the child.
	void max_cpu(unsigned lim); // in seconds
	void max_mem(unsigned lim); // in megabytes
	
	//To start the sandbox, use process::launch(). Other process:: functions are also available.
	
	//Allows access to a file, or a directory and all of its contents. Usable both before and after launch().
	//Can not be undone, the process may already have opened the file; to be sure, destroy the process.
	void permit(cstring path, bool writable) { permit_at(path, path, writable); }
	//To allow moving the filesystem around. For example, /
	void permit_at(cstring real, cstring mount_at, bool writable);
	
	~sandproc();
};

class sandcomm : nomove {
public:
	//For thread safety purposes, the parent and child count as interacting with different objects.
	//However, only one thread in each process may interact with the sandcomm object, or its associated sandproc.
	
	//If the process isn't in an Arlib sandbox, or parent didn't call connect(), returns NULL. Can only be called once.
	static sandcomm* connect();
	
	//Used to synchronize the two processes. 'chan' can be any number 0 through 7. Negative channels may be used internally.
	//While this may look like a mutex, it's also usable as event; release and wait can be in different processes.
	//Multiple threads may be in these functions simultaneously (or in these plus something else),
	// as long as only one thread is in each channel.
	//The channels start in the locked state.
	void wait(int chan);
	bool try_wait(int chan);
	void release(int chan);
	
	//To allow synchronized(box.channel(1)) {}
	struct channel_t
	{
		sandcomm* parent; int id;
		void lock() { parent->wait(id); }
		bool try_lock() { return parent->try_wait(id); }
		void unlock() { parent->release(id); }
	};
	channel_t channel(int id) { channel_t ret; ret.parent=this; ret.id=id; return ret; }
	
	
	//Clones a file handle into the child. The handle remains open in the parent. The child may get another ID.
	//Like shalloc(), fd_recv may block. Obviously both processes must use the same order.
	//If there are more than 8 handles passed to fd_send() that the child hasn't fetched from fd_recv(), behavior is undefined.
	void fd_send(intptr_t fd); // Parent only.
	intptr_t fd_recv(); // Child only.
	
	//Allocates memory shared between the two processes.
	//Limitations:
	//- Counts as a fd_send/fd_recv pair; must be done in the same order, can overflow
	//- May be expensive, prefer reusing allocations if needed (or use stdin/stdout)
	//- Can't resize an allocation, you have to create a new one and copy the data
	//- Can fail and return NULL, obviously (if parent fails, child will too; parent may succeed if child doesn't)
	//- The returned memory is initially zero initialized (or whatever the parent has written)
	void* malloc(size_t bytes);
	void free(void* data);
	
	//Convenience function, just calls the above.
	template<typename T> T* malloc(size_t count=1) { return (T*)this->malloc(sizeof(T)*count); }
	
	~sandcomm() {}
};

//Convenience wrapper, launches the current process and calls a function in it.
class sandfunc {
public:
	static void enter(); // Must be the first thing in main(), before window_init() or similar.
	sandcomm* launch(void(*proc)(sandcomm* comm)); // Not the function<> template, pointers can't be passed around like that.
};
#endif
*/
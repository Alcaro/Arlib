#pragma once
#include "../global.h"

#ifdef ARLIB_SANDBOX
//Allows safely executing untrusted code.
//
//Technical specification:
// If the child process is running hostile code, the parent may deadlock or _exit(), but may not crash or use an invalid handle.
// It is implementation defined whether an access violation will fail in the child, or kill it entirely.
void sandbox_enter(int argc, char** argv);
class sandbox : nocopy {
public:
	//Must be the first thing in main(), before window_init() and similar.
	//If the process is created via sandbox::create, this doesn't return. Otherwise, it does nothing.
	static void enter(int argc, char** argv);
	
	struct params {
		enum flags_t {
			//Always allowed: Allocate memory, synchronization operations, terminate self.
			
			//stdout and stderr go to the same places as in the parent. Default is /dev/null.
			allow_stdout = 1,
			
			//Grants access to all file operations (subject to the usual UID checks, of course). See also file_access.
			allow_file = 2,
			
			//Creates a sandbox facade; it acts like a normal sandbox, but the child process isn't restricted. It could even be a thread in the same process.
			no_security = 4,
		};
		unsigned int flags;
		
		//The operating system decides how memory is counted and how to round this.
		//0 means unlimited; anything else is in bytes.
		size_t max_mem;
		
		//Tells how many synchronization channels and shared memory regions are available in this sandbox.
		//Both must be 8 or less.
		int n_channel;
		int n_shmem;
		
		//If the allow_file flag is not set, this is called when the child tries to open a file.
		//If it returns true, the request is granted. If false, or if the function is NULL, EACCES is returned.
		//Can be called at any time when a function is executed on this sandbox object. The sandbox manager creates a thread to handle such requests.
		function<bool(const char *, bool write)> file_access;
		
		//Called in the child. setup(), if not NULL, is called first and has access to everything; minimize the amount of code here.
		//Since this goes cross-process, passing a normal userdata won't work. Instead, it's provided by the sandbox object, via shalloc.
		void(*setup)(sandbox* box);
		void(*run)(sandbox* box);
	};
	//The params object is used only during this call. You can free it afterwards.
	static sandbox* create(const params* par);
	
	//Used to synchronize the two processes. 'chan' can be any number 0 through 7. Negative channels may be used internally.
	//While this may look like a mutex, it's also usable as event; release and wait can be in different processes.
	//Each channel may only be used by one thread on each side.
	//The channels start in the released state.
	void wait(int chan);
	bool try_wait(int chan);
	bool release(int chan); // Returns whether it changed.
	
	//Allows synchronized(box->channel(1)) {}.
	struct channel_t
	{
		sandbox* parent; int id;
		void lock() { parent->wait(id); }
		bool try_lock() { return parent->try_wait(id); }
		void unlock() { parent->release(id); }
	};
	channel_t channel(int id) { return (channel_t){this, id}; }
	
	//Allocates memory shared between the two processes. At least 8 memory areas are supported. It's
	// up to the user how to use them; a recommendation is to put a fixed-size control data block in
	// area 0, and put variable-size stuff in other areas.
	//Rules (optional if you make reasonable assumptions):
	// Both processes must call this; don't share pointers directly, they may vary between the processes.
	// The function is synchronous; neither process will return until the other has entered it. If one
	//  fails, the other does too. For this reason, the two processes must perform the same sequence
	//  of calls; they must also agree on the sizes.
	// You can resize a memory area by calling this function again with the same index. However,
	//  unlike realloc, the new contents are undefined. It is also allowed to call it with the same
	//  size as last time; this just retrieves the same pointer as last time, and doesn't need to be
	//  matched.
	// The implementation must ensure the parent does not crash even if the child's internal
	//  structures are corrupt, including but not limited to size mismatch.
	// It is safe to call this from multiple threads, but not with the same index. The threads may
	//  block as long as there are threads in this function, even if they've found their own partner,
	//  but foo.
	void* shalloc(int index, size_t bytes);
	
	//This forcibly terminates the child process. If called from the child, undefined behaviour; instead, call exit() or return from run().
	~sandbox();
	
public:
	//List of Linux security objects: https://chromium.googlesource.com/chromium/src/+/master/docs/linux_sandboxing.md
	//seccomp is enough
	
	//List of Windows security objects: https://www.chromium.org/developers/design-documents/sandbox
	//CreateFile ends up in NtCreateFile, ObjectAttributes.ObjectName = "a.cpp", ObjectAttributes.RootDirectory non-NULL (use GetFinalPathNameByHandle)
	//LoadLibrary ends up in NtOpenFile, ObjectAttributes.ObjectName = "\??\C:\Windows\system32\opengl32.dll" - not \\?\ for whatever reason
	//The Chrome sandbox <https://code.google.com/p/chromium/codesearch#chromium/src/sandbox/win/src/> is barely comprehensible;
	//  it does cross-process shenanigans and can somehow call the original functions. It looks like it hijacks every DLL load and replaces the export table.
	// I don't need that, replacing a few selected functions is enough.
	//Chrome also seems to initially contact its children via WriteProcessMemory. I could steal that...
	//But I need a way to initialize the Linux version too. Preferably without ptrace. (dup something to fd 3?)
	//
	//But the real issue isn't getting things working - it's getting things to NOT work. Denying all syscalls that can be denied.
	//And unlike Linux seccomp, there is no way to say "default deny all syscalls, allow only this whitelist".
	//I have to lock everything I can find and hope the list is comprehensive, and I don't like that model. It makes me paranoid I missed something obscure.
	//
	//I'll just make sure I pass the Chrome self tests. That should be enough for all practical purposes.
	
	//Chrome sandbox entry points: http://stackoverflow.com/questions/1590337/using-the-google-chrome-sandbox
	
	struct impl;
	impl * m;
	
private:
	sandbox(){}
	sandbox(impl* m) : m(m) {}
};
#endif

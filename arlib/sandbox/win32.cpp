#ifdef _WIN32
#include "sandbox.h"
#include <windows.h>
#include <stdlib.h>

struct sandbox::impl {
	//handle type annotations per https://msdn.microsoft.com/en-us/library/windows/desktop/ms724515%28v=vs.85%29.aspx
	//child process control block is in shared memory, so parent can update it
	//parent control block is unshared (child isn't trusted, and handle values vary between processes)
	
	HANDLE childproc; // process
	
	HANDLE shalloc_handle[8]; // file mapping
	void* shalloc_ptr[8];
	//size_t shalloc_size[8]; // use VirtualQuery instead
	
	HANDLE channel_handle[8]; // event
	
	//For initialization purposes.
	void(*setup)(sandbox* box);
	void(*run)(sandbox* box);
	
	//bitwise
	//0x01 - there is an ongoing transaction
	//0x80 - the waiting queue is not empty
	//int because msvc doesn't support single-byte cmpxchg
	int fopen_status;
	
	enum {
		//you always get read access
		o_write   = 0x01,
		o_append  = 0x02,
		o_replace = 0x04,
		
		o_exec    = 0x10, // this execute flag isn't present in CreateFileW, not sure what it actually is
		o_seq     = 0x20,
		o_random  = 0x40,
	};
	uint8_t fopen_flags;
	char fopen_path[PATH_MAX];
	
	HANDLE fopen_parent_wake; // event (this handle is shared between all sandboxes)
	HANDLE fopen_child_wake; // event
	HANDLE fopen_ret; // file
	//If two threads try to fopen at once.
	HANDLE fopen_queue; // event
	
	//To open a file, the child must:
	// cmpxchg fopen_status from 0 to 1.
	// If failure:
	//  Ensure fopen_queue exists. If it didn't, restart.
	//  cmpxchg fopen_status.0x80 to true.
	//  If success, or if the old value was the desired target value already, sleep on fopen_queue.
	//  Restart.
	// 
	// Set fopen_flags and fopen_path, converting from UTF-16 to UTF-8 and from the native flags to something simpler.
	// Set fopen_ret to NULL.
	// Signal fopen_parent_wake.
	// Sleep on fopen_child_wake.
	// Copy fopen_ret to the stack.
	// xchg fopen_status to 0.
	//  If the 0x80 bit was set:
	//  Ensure fopen_queue exists.
	//  Signal fopen_queue, whether it existed or not.
	//
	//To ensure fopen_queue exists:
	// If fopen_queue is not NULL, return that it does exist.
	// Create an event object.
	// cmpxchg fopen_queue from NULL to this object. If failure, delete the created object.
	// Return that it did not exist.
	
	//The parent will:
	// Sleep on fopen_parent_wake.
	// For every sandbox:
	//  If fopen_status is not set, skip this one.
	//  Copy fopen_flags and fopen_path to local memory.
	//  If it's a system file, pretend the policy function will return true for read and false for write.
	//  Call the policy function.
	//  If success:
	//   Convert from UTF-8 to UTF-16.
	//   Call NtOpenFile with the local parameters, converting from fopen_flags to the native values.
	//   If failure, break.
	//   Duplicate the handle and put it in fopen_ret.
	//   Close the local handle.
	//  Signal fopen_child_wake.
	// Restart.
};

static HANDLE shmem_par = NULL;

void sandbox::enter(int argc, char** argv)
{
	if (!shmem_par) return;
	
	sandbox::impl* box = (sandbox::impl*)MapViewOfFile(shmem_par, FILE_MAP_WRITE, 0,0, 65536);
	
	sandbox boxw(box);
	if (box->setup) box->setup(&boxw);
	
	//TODO: lockdown
	
	box->run(&boxw);
	exit(0);
}



static WCHAR* selfpath()
{
	static WCHAR* ret = NULL;
	if (ret) return ret;
	
	DWORD len = MAX_PATH;
again:
	WCHAR* ptr = malloc(sizeof(WCHAR)*len);
	DWORD gmfnret = GetModuleFileNameW(NULL, ptr, len);
	if (!gmfnret || gmfnret==len)
	{
		free(ptr);
		len*=2;
		goto again;
	}
	
	//ensure thread safety
	WCHAR* prevret = (WCHAR*)InterlockedCompareExchangePointer((void**)&ret, ptr, NULL);
	if (prevret == NULL)
	{
		return ptr;
	}
	else
	{
		free(ptr);
		return prevret;
	}
}

sandbox* sandbox::create(const params* par)
{
	STARTUPINFOW sti;
	memset(&sti, 0, sizeof(sti));
	sti.cb=sizeof(sti);
	
	PROCESS_INFORMATION pi;
	CreateProcessW(selfpath(), NULL, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &sti, &pi);
	
	//this assumes the allocation granularity is at least as big as the shared control block
	//no system has page size below 4096, and I need just a few hundred, so that's safe (and it's 65536 in practice)
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	HANDLE shmem = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,si.dwAllocationGranularity, NULL);
	
	HANDLE shmem_tar;
	DuplicateHandle(GetCurrentProcess(), shmem, pi.hProcess, &shmem_tar, 0, FALSE, DUPLICATE_SAME_ACCESS);
	SIZE_T ignore;
	WriteProcessMemory(pi.hProcess, &shmem_par, &shmem_tar, sizeof(HANDLE), &ignore);
	
	sandbox::impl* m = (sandbox::impl*)MapViewOfFile(shmem, FILE_MAP_WRITE, 0,0, si.dwAllocationGranularity);
	memset(m, 0, sizeof(*m));
	
	m->setup = par->setup;
	m->run = par->run;
	
	ResumeThread(pi.hThread);
	return new sandbox(m);
}

void sandbox::wait(int chan)
{
}

bool sandbox::try_wait(int chan)
{
	return true;
}

bool sandbox::release(int chan)
{
	return true;
}

void* sandbox::shalloc(int index, size_t bytes)
{
	return NULL;
}

sandbox::~sandbox()
{
	TerminateProcess(m->childproc, 0);
	
	free(m);
}
#endif

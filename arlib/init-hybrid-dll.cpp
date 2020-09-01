#if defined(_WIN32) && defined(ARLIB_HYBRID_DLL)
// be careful with Arlib headers, we can't use OS facilities in this file

#include <windows.h>
#include <winternl.h>
#include <stdint.h>

#if defined(__i386__) || defined(__x86_64__)
# define MAYBE_SSE2
# include <emmintrin.h>
#endif

#define ALLINTS(x) x(uint8_t) // just ignore the others...
#include "thread/atomic.h"

extern "C" void _pei386_runtime_relocator();
extern "C" int __cpu_indicator_init(void);

namespace {

bool streq(const char * a, const char * b) // no strcmp, it's an OS facility
{
	while (true)
	{
		if (*a != *b) return false;
		if (!*a) return true;
		a++;
		b++;
	}
}

#define strlen my_strlen
size_t strlen(const char * a)
{
	const char * b = a;
	while (*b) b++;
	return b-a;
}


HMODULE pe_get_ntdll()
{
	PEB* peb;
#ifdef __i386__
	__asm__("{mov %%fs:(0x30), %0|mov %0, fs:[0x30]}" : "=r"(peb)); // *(PEB* __seg_fs*)0x30 would be a lot cleaner, but it's C only
#elif defined(__x86_64__)
	__asm__("{mov %%gs:(0x60), %0|mov %0, gs:[0x60]}" : "=r"(peb));
#elif defined(_M_IX86)
	peb = (PEB*)__readfsdword(0x30);
#elif _M_AMD64
	peb = (PEB*)__readgsqword(0x60);
#else
	#error "don't know what platform this is"
#endif
	
	// windows maintains a list of all DLLs in the process, available via PEB (available via TEB)
	// in three different orders - load order, memory order, and init order
	// ntdll is always #2 in load order, with the exe being #1 (http://www.nynaeve.net/?p=185)
	// (kernel32 is probably also always present, but I don't think that's guaranteed, and I don't need kernel32 anyways)
	// only parts of the relevant structs are documented, so this is based on
	// https://github.com/wine-mirror/wine/blob/master/include/winternl.h
	PEB_LDR_DATA* pld = peb->Ldr;
	LDR_DATA_TABLE_ENTRY* ldte_exe = (LDR_DATA_TABLE_ENTRY*)pld->Reserved2[1];
	LDR_DATA_TABLE_ENTRY* ldte_ntdll = (LDR_DATA_TABLE_ENTRY*)ldte_exe->Reserved1[0];
	return (HMODULE)ldte_ntdll->DllBase;
}

IMAGE_NT_HEADERS* pe_get_nt_headers(HMODULE mod)
{
	uint8_t* base_addr = (uint8_t*)mod;
	IMAGE_DOS_HEADER* head_dos = (IMAGE_DOS_HEADER*)base_addr;
	return (IMAGE_NT_HEADERS*)(base_addr + head_dos->e_lfanew);
}
IMAGE_DATA_DIRECTORY* pe_get_section_header(HMODULE mod, int sec)
{
	return &pe_get_nt_headers(mod)->OptionalHeader.DataDirectory[sec];
}
void* pe_get_section_body(HMODULE mod, int sec)
{
	return (uint8_t*)mod + pe_get_section_header(mod, sec)->VirtualAddress;
}

void* pe_get_proc_address(HMODULE mod, const char * name)
{
	if (!mod) return NULL;
	
	uint8_t* base_addr = (uint8_t*)mod;
	IMAGE_EXPORT_DIRECTORY* exports = (IMAGE_EXPORT_DIRECTORY*)pe_get_section_body(mod, IMAGE_DIRECTORY_ENTRY_EXPORT);
	
	DWORD * addr_off = (DWORD*)(base_addr + exports->AddressOfFunctions);
	DWORD * name_off = (DWORD*)(base_addr + exports->AddressOfNames);
	WORD * ordinal = (WORD*)(base_addr + exports->AddressOfNameOrdinals);
	
	if ((uintptr_t)name < 0x10000) // ordinal
	{
		size_t idx = (uintptr_t)name - exports->Base;
		if (idx > exports->NumberOfFunctions)
			return NULL;
		return base_addr + addr_off[idx];
	}
	
	for (size_t i=0;i<exports->NumberOfNames;i++)
	{
		const char * exp_name = (const char*)(base_addr + name_off[i]);
		if (streq(name, exp_name))
			return base_addr + addr_off[ordinal[i]];
	}
	return NULL;
}



struct ntdll_t {
NTSTATUS WINAPI (*LdrLoadDll)(const WCHAR * DirPath, DWORD Flags, const UNICODE_STRING * ModuleFileName, HMODULE* ModuleHandle);
HMODULE WINAPI (*RtlPcToFileHeader)(void* PcValue, HMODULE* BaseOfImage);
NTSTATUS WINAPI (*NtProtectVirtualMemory)(HANDLE process, void** addr_ptr, size_t* size_ptr, uint32_t new_prot, uint32_t* old_prot);
//"everyone" knows LdrProcessRelocationBlock returns IMAGE_BASE_RELOCATION*, but does it, or does it return void*?
//or does it return void and the end iterator is just leftover in %rax?
IMAGE_BASE_RELOCATION* WINAPI (*LdrProcessRelocationBlock)(void* page, unsigned count, uint16_t* relocs, intptr_t delta);
};
#define ntdll_t_names \
	"LdrLoadDll\0" \
	"RtlPcToFileHeader\0" \
	"NtProtectVirtualMemory\0" \
	"LdrProcessRelocationBlock\0" \


void pe_get_ntdll_syms(ntdll_t* out)
{
	void* * fp = (void**)out;
	const char * names = ntdll_t_names;
	
	HMODULE mod = pe_get_ntdll();
	while (*names)
	{
		*fp = pe_get_proc_address(mod, names);
		fp++;
		names += strlen(names)+1;
	}
}

void pe_process_imports(ntdll_t* ntdll, HMODULE mod)
{
	uint8_t* base_addr = (uint8_t*)mod;
	
	IMAGE_IMPORT_DESCRIPTOR* imports = (IMAGE_IMPORT_DESCRIPTOR*)pe_get_section_body(mod, IMAGE_DIRECTORY_ENTRY_IMPORT);
	while (imports->Name)
	{
		const char * libname = (char*)(base_addr + imports->Name);
		WCHAR libname16[64];
		WCHAR* libname16iter = libname16;
		while (*libname) *libname16iter++ = *libname++;
		*libname16iter = '\0';
		
		UNICODE_STRING libname_us = { (uint16_t)((libname16iter-libname16)*sizeof(WCHAR)), sizeof(libname16), libname16 };
		
		HMODULE mod;
		if (FAILED(ntdll->LdrLoadDll(NULL, 0, &libname_us, &mod))) mod = NULL;
		
		void* * out = (void**)(base_addr + imports->FirstThunk);
		uintptr_t* thunks = (uintptr_t*)(base_addr + (imports->OriginalFirstThunk ? imports->OriginalFirstThunk : imports->FirstThunk));
		
		while (*thunks)
		{
			IMAGE_IMPORT_BY_NAME* imp = (IMAGE_IMPORT_BY_NAME*)(base_addr + *thunks);
			*out = pe_get_proc_address(mod, (char*)imp->Name);
			thunks++;
			out++;
		}
		
		imports++;
	}
}

void pe_do_relocs(ntdll_t* ntdll, HMODULE mod)
{
	uint8_t* base_addr = (uint8_t*)mod;
	
	IMAGE_NT_HEADERS* head_nt = pe_get_nt_headers(mod);
	uint8_t* orig_base_addr = (uint8_t*)head_nt->OptionalHeader.ImageBase;
	
	intptr_t delta = base_addr - orig_base_addr;
	if (!delta) return;
	
	uint32_t prot_prev[32]; // static allocation, malloc doesn't work yet... a normal exe has 19 sections, hope it won't grow above 32
	IMAGE_SECTION_HEADER* sec = (IMAGE_SECTION_HEADER*)((uint8_t*)&head_nt->OptionalHeader + head_nt->FileHeader.SizeOfOptionalHeader);
	for (uint16_t i=0;i<head_nt->FileHeader.NumberOfSections;i++)
	{
		// ideally, there would be no relocations in .text, so we can just skip that section
		// in practice, __CTOR_LIST__ is in the same page, so let's mark everything PAGE_EXECUTE_READWRITE instead of PAGE_READWRITE
		// we're already deep into shenanigans territory, a W^X violation is nothing to worry about
		void* sec_addr = base_addr + sec[i].VirtualAddress;
		size_t sec_size = sec[i].SizeOfRawData;
		ntdll->NtProtectVirtualMemory((HANDLE)-1, &sec_addr, &sec_size, PAGE_EXECUTE_READWRITE, &prot_prev[i]);
	}
	
	IMAGE_DATA_DIRECTORY* relocs = pe_get_section_header(mod, IMAGE_DIRECTORY_ENTRY_BASERELOC);
	uint8_t* iter = base_addr + relocs->VirtualAddress;
	uint8_t* end = iter + relocs->Size;
	while (iter < end)
	{
		IMAGE_BASE_RELOCATION* reloc = (IMAGE_BASE_RELOCATION*)iter;
		ntdll->LdrProcessRelocationBlock(base_addr + reloc->VirtualAddress,
		                                 (reloc->SizeOfBlock - sizeof(*reloc))/sizeof(uint16_t),
		                                 (uint16_t*)(iter + sizeof(*reloc)), delta);
		iter += reloc->SizeOfBlock;
	}
	
	for (uint16_t i=0;i<head_nt->FileHeader.NumberOfSections;i++)
	{
		void* sec_addr = base_addr + sec[i].VirtualAddress;
		size_t sec_size = sec[i].SizeOfRawData;
		ntdll->NtProtectVirtualMemory((HANDLE)-1, &sec_addr, &sec_size, prot_prev[i], &prot_prev[0]);
	}
}

enum {
	init_no,
	init_busy,
	init_done
};
static uint8_t init_state = init_no;

}

__attribute__((constructor))
static void arlib_hybrid_exe_init() // if we're an exe, ctors will run before the dll init
{
	init_state = init_done;
}

void arlib_hybrid_dll_init();
void arlib_hybrid_dll_init()
{
	int state = lock_read_acq(&init_state);
	if (state == init_done) return;
	
#ifdef ARLIB_THREAD
	state = lock_cmpxchg_acq(&init_state, init_no, init_busy);
	if (state == init_busy)
	{
		while (state == init_busy)
		{
#ifdef MAYBE_SSE2
			_mm_pause();
#endif
			state = lock_read_acq(&init_state);
		}
		return;
	}
#endif
	
	ntdll_t ntdll;
	pe_get_ntdll_syms(&ntdll);
	HMODULE this_mod;
	pe_process_imports(&ntdll, ntdll.RtlPcToFileHeader((void*)arlib_hybrid_dll_init, &this_mod));
	pe_do_relocs(&ntdll, this_mod);
	
	_pei386_runtime_relocator(); // this doesn't seem to do much, but no reason not to
	
	// ignore ctors, to ensure use of global variables is swiftly punished
	__cpu_indicator_init(); // except this one - non-allocating globals are fine
	/*
	typedef void(*ctor_t)();
	extern ctor_t __CTOR_LIST__[];
	extern ctor_t __DTOR_LIST__[];
	
	ctor_t * iter = __CTOR_LIST__+1;
	while (*iter)
	{
		(*iter)();
		iter++;
	}
	*/
	
	lock_write_rel(&init_state, init_done);
}
#endif

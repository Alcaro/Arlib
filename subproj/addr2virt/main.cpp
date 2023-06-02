#include "arlib.h"

// Takes one or more physical addresses in the file. Returns the virtual address when mapped into memory.

#ifdef _WIN32
#include <windows.h>
#else
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef uint64_t ULONGLONG;
typedef int32_t LONG;

#define IMAGE_DOS_SIGNATURE 0x5A4D
#define IMAGE_NT_SIGNATURE 0x00004550

struct IMAGE_DOS_HEADER {
  WORD e_magic;
  WORD e_cblp;
  WORD e_cp;
  WORD e_crlc;
  WORD e_cparhdr;
  WORD e_minalloc;
  WORD e_maxalloc;
  WORD e_ss;
  WORD e_sp;
  WORD e_csum;
  WORD e_ip;
  WORD e_cs;
  WORD e_lfarlc;
  WORD e_ovno;
  WORD e_res[4];
  WORD e_oemid;
  WORD e_oeminfo;
  WORD e_res2[10];
  LONG e_lfanew;
};

#define IMAGE_FILE_MACHINE_I386 0x014c
#define IMAGE_FILE_MACHINE_AMD64 0x8664

struct IMAGE_FILE_HEADER {
  WORD Machine;
  WORD NumberOfSections;
  DWORD TimeDateStamp;
  DWORD PointerToSymbolTable;
  DWORD NumberOfSymbols;
  WORD SizeOfOptionalHeader;
  WORD Characteristics;
};

struct IMAGE_DATA_DIRECTORY {
  DWORD VirtualAddress;
  DWORD Size;
};

#define IMAGE_NUMBEROF_DIRECTORY_ENTRIES 16

struct IMAGE_OPTIONAL_HEADER32 {
  WORD Magic;
  BYTE MajorLinkerVersion;
  BYTE MinorLinkerVersion;
  DWORD SizeOfCode;
  DWORD SizeOfInitializedData;
  DWORD SizeOfUninitializedData;
  DWORD AddressOfEntryPoint;
  DWORD BaseOfCode;
  DWORD BaseOfData;
  DWORD ImageBase;
  DWORD SectionAlignment;
  DWORD FileAlignment;
  WORD MajorOperatingSystemVersion;
  WORD MinorOperatingSystemVersion;
  WORD MajorImageVersion;
  WORD MinorImageVersion;
  WORD MajorSubsystemVersion;
  WORD MinorSubsystemVersion;
  DWORD Win32VersionValue;
  DWORD SizeOfImage;
  DWORD SizeOfHeaders;
  DWORD CheckSum;
  WORD Subsystem;
  WORD DllCharacteristics;
  DWORD SizeOfStackReserve;
  DWORD SizeOfStackCommit;
  DWORD SizeOfHeapReserve;
  DWORD SizeOfHeapCommit;
  DWORD LoaderFlags;
  DWORD NumberOfRvaAndSizes;
  IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];
};

struct IMAGE_OPTIONAL_HEADER64 {
  WORD Magic;
  BYTE MajorLinkerVersion;
  BYTE MinorLinkerVersion;
  DWORD SizeOfCode;
  DWORD SizeOfInitializedData;
  DWORD SizeOfUninitializedData;
  DWORD AddressOfEntryPoint;
  DWORD BaseOfCode;
  ULONGLONG ImageBase;
  DWORD SectionAlignment;
  DWORD FileAlignment;
  WORD MajorOperatingSystemVersion;
  WORD MinorOperatingSystemVersion;
  WORD MajorImageVersion;
  WORD MinorImageVersion;
  WORD MajorSubsystemVersion;
  WORD MinorSubsystemVersion;
  DWORD Win32VersionValue;
  DWORD SizeOfImage;
  DWORD SizeOfHeaders;
  DWORD CheckSum;
  WORD Subsystem;
  WORD DllCharacteristics;
  ULONGLONG SizeOfStackReserve;
  ULONGLONG SizeOfStackCommit;
  ULONGLONG SizeOfHeapReserve;
  ULONGLONG SizeOfHeapCommit;
  DWORD LoaderFlags;
  DWORD NumberOfRvaAndSizes;
  IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];
};

struct IMAGE_NT_HEADERS32 {
  DWORD Signature;
  IMAGE_FILE_HEADER FileHeader;
  IMAGE_OPTIONAL_HEADER32 OptionalHeader;
};

struct IMAGE_NT_HEADERS64 {
  DWORD Signature;
  IMAGE_FILE_HEADER FileHeader;
  IMAGE_OPTIONAL_HEADER64 OptionalHeader;
};

#define IMAGE_FIRST_SECTION32(ntheader) ((IMAGE_SECTION_HEADER*) ((size_t)ntheader + offsetof(IMAGE_NT_HEADERS32,OptionalHeader) + ((IMAGE_NT_HEADERS32*)(ntheader))->FileHeader.SizeOfOptionalHeader))
#define IMAGE_FIRST_SECTION64(ntheader) ((IMAGE_SECTION_HEADER*) ((size_t)ntheader + offsetof(IMAGE_NT_HEADERS64,OptionalHeader) + ((IMAGE_NT_HEADERS64*)(ntheader))->FileHeader.SizeOfOptionalHeader))

#define IMAGE_SIZEOF_SHORT_NAME 8

struct IMAGE_SECTION_HEADER {
  BYTE Name[IMAGE_SIZEOF_SHORT_NAME];
  union {
	DWORD PhysicalAddress;
	DWORD VirtualSize;
  } Misc;
  DWORD VirtualAddress;
  DWORD SizeOfRawData;
  DWORD PointerToRawData;
  DWORD PointerToRelocations;
  DWORD PointerToLinenumbers;
  WORD NumberOfRelocations;
  WORD NumberOfLinenumbers;
  DWORD Characteristics;
};

#define IMAGE_DIRECTORY_ENTRY_EXPORT 0
#define IMAGE_DIRECTORY_ENTRY_IMPORT 1
#define IMAGE_DIRECTORY_ENTRY_RESOURCE 2
#define IMAGE_DIRECTORY_ENTRY_EXCEPTION 3
#define IMAGE_DIRECTORY_ENTRY_SECURITY 4
#define IMAGE_DIRECTORY_ENTRY_BASERELOC 5
#define IMAGE_DIRECTORY_ENTRY_DEBUG 6
#define IMAGE_DIRECTORY_ENTRY_ARCHITECTURE 7
#define IMAGE_DIRECTORY_ENTRY_GLOBALPTR 8
#define IMAGE_DIRECTORY_ENTRY_TLS 9
#define IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG 10
#define IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT 11
#define IMAGE_DIRECTORY_ENTRY_IAT 12
#define IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT 13
#define IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR 14

struct IMAGE_BASE_RELOCATION {
  DWORD VirtualAddress;
  DWORD SizeOfBlock;
};
#define IMAGE_REL_BASED_ABSOLUTE 0
#define IMAGE_REL_BASED_HIGHLOW 3
#define IMAGE_REL_BASED_DIR64 10

struct IMAGE_IMPORT_DESCRIPTOR {
  union {
	DWORD Characteristics;
	DWORD OriginalFirstThunk;
  };
  DWORD TimeDateStamp;

  DWORD ForwarderChain;
  DWORD Name;
  DWORD FirstThunk;
};

struct IMAGE_IMPORT_BY_NAME {
  WORD Hint;
  BYTE Name[1];
};

#define IMAGE_ORDINAL_FLAG32 0x80000000
#define IMAGE_SNAP_BY_ORDINAL32(Ordinal) ((Ordinal & IMAGE_ORDINAL_FLAG32)!=0)
#define IMAGE_SNAP_BY_ORDINAL(Ordinal) IMAGE_SNAP_BY_ORDINAL32(Ordinal)

struct IMAGE_EXPORT_DIRECTORY {
  DWORD Characteristics;
  DWORD TimeDateStamp;
  WORD MajorVersion;
  WORD MinorVersion;
  DWORD Name;
  DWORD Base;
  DWORD NumberOfFunctions;
  DWORD NumberOfNames;
  DWORD AddressOfFunctions;
  DWORD AddressOfNames;
  DWORD AddressOfNameOrdinals;
};

#define IMAGE_SCN_CNT_INITIALIZED_DATA 0x00000040
#define IMAGE_SCN_CNT_UNINITIALIZED_DATA 0x00000080

#define IMAGE_SCN_MEM_EXECUTE 0x20000000
#define IMAGE_SCN_MEM_READ 0x40000000
#define IMAGE_SCN_MEM_WRITE 0x80000000

#endif

struct section {
	uint64_t vma;
	uint64_t size;
	uint64_t file_off;
};

static bool import(bytesr data, array<section>& ret)
{
	size_t size = data.size();
	
	IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)data.ptr();
	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
		return false;
	
	IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)(data.ptr() + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE)
		return false;
	if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64)
		return false;
	
	uint64_t vma = nt->OptionalHeader.ImageBase;
	
	IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION64(nt);
	IMAGE_SECTION_HEADER* sec_last = sec + nt->FileHeader.NumberOfSections;
	while (sec < sec_last)
	{
		section& sec_out = ret.append();
		sec_out.vma = vma + sec->VirtualAddress;
		sec_out.size = sec->SizeOfRawData;
		sec_out.file_off = sec->PointerToRawData;
		sec++;
	}
	
	return true;
}

int main(int argc, char** argv)
{
	auto map = file2::mmap(argv[1]);
	
	array<section> secs;
	if (!import(map, secs))
	{
		puts("??");
	}
	char** iter = argv+2;
	while (*iter)
	{
		uint64_t file_addr;
		fromstringhex(*iter, file_addr);
		for (section& sec : secs)
		{
			if (file_addr >= sec.file_off && file_addr < sec.file_off+sec.size)
			{
				printf("%lx -> %lx\n", file_addr, file_addr-sec.file_off+sec.vma);
			}
		}
		iter++;
	}
	return 0;
}

// MIT License
//
// Copyright (c) 2016 Alfred Agrell
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

//The above license applies only to this file, not the entire Arlib.

//See wutf.h for documentation.

#ifdef _WIN32
#include "wutf.h"
#include <windows.h>

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS 0x00000000
#endif
#ifndef STATUS_BUFFER_OVERFLOW
#define STATUS_BUFFER_OVERFLOW 0x80000005
#endif

static NTSTATUS WINAPI
RtlMultiByteToUnicodeN_Utf(
                PWCH   UnicodeString,
                ULONG  MaxBytesInUnicodeString,
                PULONG BytesInUnicodeString,
          const CHAR   *MultiByteString,
                ULONG  BytesInMultiByteString)
{
	int len = WuTF_utf8_to_utf16(WUTF_TRUNCATE,
	                             MultiByteString, BytesInMultiByteString,
	                             (uint16_t*)UnicodeString, MaxBytesInUnicodeString/2);
	if (BytesInUnicodeString) *BytesInUnicodeString = len*2;
	return STATUS_SUCCESS;
}

static NTSTATUS WINAPI
RtlUnicodeToMultiByteN_Utf(
                PCHAR  MultiByteString,
                ULONG  MaxBytesInMultiByteString,
                PULONG BytesInMultiByteString,
                PCWCH  UnicodeString,
                ULONG  BytesInUnicodeString)
{
	int len = WuTF_utf16_to_utf8(WUTF_TRUNCATE,
	                             (uint16_t*)UnicodeString, BytesInUnicodeString/2,
	                             MultiByteString, MaxBytesInMultiByteString);
	if (BytesInMultiByteString) *BytesInMultiByteString = len;
	return STATUS_SUCCESS;
}

static NTSTATUS WINAPI
RtlMultiByteToUnicodeSize_Utf(
                PULONG BytesInUnicodeString,
          const CHAR   *MultiByteString,
                ULONG  BytesInMultiByteString)
{
	int len = WuTF_utf8_to_utf16(0,
	                             MultiByteString, BytesInMultiByteString,
	                             NULL, 0);
	*BytesInUnicodeString = len*2;
	return STATUS_SUCCESS;
}

static NTSTATUS WINAPI
RtlUnicodeToMultiByteSize_Utf(
                PULONG BytesInMultiByteString,
                PCWCH  UnicodeString,
                ULONG  BytesInUnicodeString)
{
	int len = WuTF_utf16_to_utf8(0,
	                             (uint16_t*)UnicodeString, BytesInUnicodeString/2,
	                             NULL, 0);
	*BytesInMultiByteString = len;
	return STATUS_SUCCESS;
}


//ignores invalid flags and parameters
static int WINAPI
MultiByteToWideChar_Utf(UINT CodePage, DWORD dwFlags,
                        LPCSTR lpMultiByteStr, int cbMultiByte,
                        LPWSTR lpWideCharStr, int cchWideChar)
{
	int ret = WuTF_utf8_to_utf16((dwFlags&MB_ERR_INVALID_CHARS) ? WUTF_STRICT : 0,
	                             lpMultiByteStr, cbMultiByte,
	                             (uint16_t*)lpWideCharStr, cchWideChar);
	
	if (ret<0)
	{
		if (ret == WUTF_E_STRICT) SetLastError(ERROR_NO_UNICODE_TRANSLATION);
		if (ret == WUTF_E_TRUNCATE) SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return 0;
	}
	return ret;
}

static int WINAPI
WideCharToMultiByte_Utf(UINT CodePage, DWORD dwFlags,
                        LPCWSTR lpWideCharStr, int cchWideChar,
                        LPSTR lpMultiByteStr, int cbMultiByte,
                        LPCSTR lpDefaultChar, LPBOOL lpUsedDefaultChar)
{
	int ret = WuTF_utf16_to_utf8((dwFlags&MB_ERR_INVALID_CHARS) ? WUTF_STRICT : 0,
	                             (uint16_t*)lpWideCharStr, cchWideChar,
	                             lpMultiByteStr, cbMultiByte);
	
	if (ret<0)
	{
		if (ret == WUTF_E_STRICT) SetLastError(ERROR_NO_UNICODE_TRANSLATION);
		if (ret == WUTF_E_TRUNCATE) SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return 0;
	}
	return ret;
}


//https://sourceforge.net/p/predef/wiki/Architectures/
#if defined(_M_IX86) || defined(__i386__)
static void RedirectFunction_machine(LPBYTE victim, LPBYTE replacement)
{
	victim[0] = 0xE9; // jmp <offset from next instruction>
	*(LONG_PTR*)(victim+1) = replacement-victim-5;
}

#elif defined(_M_X64) || defined(__x86_64__)
static void RedirectFunction_machine(LPBYTE victim, LPBYTE replacement)
{
	// this destroys %rax, but that register is caller-saved (and the return value).
	// https://msdn.microsoft.com/en-us/library/9z1stfyw.aspx
	victim[0] = 0x48; victim[1] = 0xB8;   // mov %rax, <64 bit constant>
	*(LPBYTE*)(victim+2) = replacement;
	victim[10] = 0xFF; victim[11] = 0xE0; // jmp %rax
}

#else
#error Not supported
#endif


static void RedirectFunction(FARPROC victim, FARPROC replacement)
{
	DWORD prot;
	// it's bad to have W+X on the same page, but I don't want to remove X from ntdll.dll.
	// if I hit NtProtectVirtualMemory, I won't be able to fix it.
	VirtualProtect((void*)victim, 64, PAGE_EXECUTE_READWRITE, &prot);
	RedirectFunction_machine((LPBYTE)victim, (LPBYTE)replacement);
	VirtualProtect((void*)victim, 64, prot, &prot);
}

void WuTF_enable()
{
	HMODULE ntdll = GetModuleHandle("ntdll.dll");
	//list of possibly relevant functions in ntdll.dll (pulled from 'strings ntdll.dll'):
	//some are documented at https://msdn.microsoft.com/en-us/library/windows/hardware/ff553354%28v=vs.85%29.aspx
	//many are implemented in terms of other functions, often rooting in the ones I've hijacked
	// RtlAnsiCharToUnicodeChar
	// RtlAnsiStringToUnicodeSize
	// RtlAnsiStringToUnicodeString
	// RtlAppendAsciizToString
	// RtlAppendPathElement
	// RtlAppendStringToString
	// RtlAppendUnicodeStringToString
	// RtlAppendUnicodeToString
	// RtlCreateUnicodeStringFromAsciiz
	// RtlMultiAppendUnicodeStringBuffer
	RedirectFunction(GetProcAddress(ntdll, "RtlMultiByteToUnicodeN"), (FARPROC)RtlMultiByteToUnicodeN_Utf);
	RedirectFunction(GetProcAddress(ntdll, "RtlMultiByteToUnicodeSize"), (FARPROC)RtlMultiByteToUnicodeSize_Utf);
	// RtlOemStringToUnicodeSize
	// RtlOemStringToUnicodeString
	// RtlOemToUnicodeN
	// RtlRunDecodeUnicodeString
	// RtlRunEncodeUnicodeString
	// RtlUnicodeStringToAnsiSize
	// RtlUnicodeStringToAnsiString
	// RtlUnicodeStringToCountedOemString
	// RtlUnicodeStringToInteger
	// RtlUnicodeStringToOemSize
	// RtlUnicodeStringToOemString
	// RtlUnicodeToCustomCPN
	RedirectFunction(GetProcAddress(ntdll, "RtlUnicodeToMultiByteN"), (FARPROC)RtlUnicodeToMultiByteN_Utf);
	RedirectFunction(GetProcAddress(ntdll, "RtlUnicodeToMultiByteSize"), (FARPROC)RtlUnicodeToMultiByteSize_Utf);
	// RtlUnicodeToOemN
	// RtlUpcaseUnicodeChar
	// RtlUpcaseUnicodeString
	// RtlUpcaseUnicodeStringToAnsiString
	// RtlUpcaseUnicodeStringToCountedOemString
	// RtlUpcaseUnicodeStringToOemString
	// RtlUpcaseUnicodeToCustomCPN
	// RtlUpcaseUnicodeToMultiByteN
	// RtlUpcaseUnicodeToOemN
	// RtlxAnsiStringToUnicodeSize
	// RtlxOemStringToUnicodeSize
	// RtlxUnicodeStringToAnsiSize
	// RtlxUnicodeStringToOemSize
	
	HMODULE kernel32 = GetModuleHandle("kernel32.dll");
	RedirectFunction(GetProcAddress(kernel32, "MultiByteToWideChar"), (FARPROC)MultiByteToWideChar_Utf);
	RedirectFunction(GetProcAddress(kernel32, "WideCharToMultiByte"), (FARPROC)WideCharToMultiByte_Utf);
}



void WuTF_args(int* argc_p, char** * argv_p)
{
	int i;
	
	int argc;
	LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
	LPSTR* argv = (LPSTR*)HeapAlloc(GetProcessHeap(), 0, sizeof(LPSTR)*(argc+1));
	
	for (i=0;i<argc;i++)
	{
		int cb = WuTF_utf16_to_utf8(0, (uint16_t*)wargv[i], -1, NULL, 0);
		argv[i] = (char*)HeapAlloc(GetProcessHeap(), 0, cb);
		WuTF_utf16_to_utf8(0, (uint16_t*)wargv[i], -1, argv[i], cb);
	}
	argv[argc]=0;
	
	*argv_p = argv;
	*argc_p = argc;
}

#endif

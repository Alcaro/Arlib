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

// It is well known that Windows supports two flavors of every(*) function that
// takes or returns strings: A and W. The A ones take strings in the local
// user's codepage; W uses UTF-16.
// It is also fairly well known that the system codepage can not be set to UTF-8.
// 
//
// (*) With the exception of CommandLineToArgvW.
//
// - https://wiki.winehq.org/Cygwin_and_More#The_Audience_Applauds

#ifdef _WIN32
#include "wutf.h"
#include <windows.h>

#define DEBUG 2

#if DEBUG>0
#include <stdio.h>
#endif

//always uses CP_UTF8; ignores invalid flags and parameters for that code page
static int WINAPI
MultiByteToWideChar_Utf(UINT CodePage, DWORD dwFlags,
                        LPCSTR lpMultiByteStr, int cbMultiByte,
                        LPWSTR lpWideCharStr, int cchWideChar)
{
	int ret = WuTF_utf8_to_utf16(dwFlags&MB_ERR_INVALID_CHARS,
	                             lpMultiByteStr, cbMultiByte,
	                             (uint16_t*)lpWideCharStr, cchWideChar);
	if (ret<0)
	{
		if (ret==-1) SetLastError(ERROR_NO_UNICODE_TRANSLATION);
		if (ret==-2) SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return 0;
	}
	
	return ret;
}

//same caveats as MultiByteToWideChar_Utf
int WINAPI
WideCharToMultiByte_Utf(UINT CodePage, DWORD dwFlags,
                        LPCWSTR lpWideCharStr, int cchWideChar,
                        LPSTR lpMultiByteStr, int cbMultiByte,
                        LPCSTR lpDefaultChar, LPBOOL lpUsedDefaultChar)
{
	return 0;
}


//originally ANSI_STRING and UNICODE_STRING
//renamed to avoid conflicts if some versions of windows.h includes the real ones
struct MS_ANSI_STRING {
	USHORT Length;
	USHORT MaximumLength;
	PCHAR Buffer;
};

struct MS_UNICODE_STRING {
	USHORT Length;
	USHORT MaximumLength; // lengths are bytes (cb), not characters (cch)
	PWSTR Buffer;
};


#if DEBUG>=2
static void PrintAnsiString(const char * prefix, const struct MS_ANSI_STRING * String)
{
	int i;
	printf("%s(cchLen=%i,cchMax=%i):", prefix, String->Length, String->MaximumLength);
	for(i=0;i<=String->Length;i++)
	{
		printf("[%c]", String->Buffer[i]&255);
	}
	puts("");
}

static void PrintUnicodeString(const char * prefix, const struct MS_UNICODE_STRING * String)
{
	int i;
	printf("%s(cchLen=%i,cchMax=%i):", prefix, String->Length/2, String->MaximumLength/2);
	for(i=0;i<=String->Length/2;i++)
	{
		printf("[%c]", String->Buffer[i]&255);
	}
	puts("");
}
#endif

static NTSTATUS WINAPI
RtlAnsiStringToUnicodeString_UTF8(
                struct MS_UNICODE_STRING * DestinationString,
                const struct MS_ANSI_STRING * SourceString,
                BOOLEAN AllocateDestinationString)
{
	int cchMaxOut;
	int cchLen;
	
#if DEBUG>=2
	PrintAnsiString("IN:ANSI", SourceString);
#endif
	
	if (AllocateDestinationString)
	{
		cchLen = MultiByteToWideChar_Utf(CP_UTF8, 0,
		                                 SourceString->Buffer, SourceString->Length,
		                                 NULL, 0);
		
printf("###[%i]\n",cchLen);
		if (cchLen >= 0x10000/2) return 0x80000005; // STATUS_BUFFER_OVERFLOW
		if (cchLen+1 < 0x10000/2) cchLen += 1; // NUL terminator
		DestinationString->MaximumLength = cchLen*2;
		// this pointer is sent to RtlFreeUnicodeString, which ends up in RtlFreeHeap
		// this is quite clearly mapped up to RtlAllocHeap, but HeapAlloc forwards to that anyways.
		// and GetProcessHeap is verified to return the same thing as RtlFreeUnicodeString uses.
		DestinationString->Buffer = (PWSTR)HeapAlloc(GetProcessHeap(), 0, DestinationString->MaximumLength);
	}
	
	cchMaxOut = DestinationString->MaximumLength/2;
	cchLen = MultiByteToWideChar_Utf(CP_UTF8, 0,
	                                 SourceString->Buffer, SourceString->Length,
	                                 DestinationString->Buffer, cchMaxOut);
printf("###[%i %i]\n",cchLen,DestinationString->MaximumLength);
	if (!cchLen || cchLen > cchMaxOut) return 0x80000005; // STATUS_BUFFER_OVERFLOW
	
	DestinationString->Length = cchLen*2;
	if (cchLen < cchMaxOut) DestinationString->Buffer[cchLen] = '\0';
#if DEBUG>=2
	PrintUnicodeString("OUT:UNI", DestinationString);
#endif
	return 0x00000000; // STATUS_SUCCESS
}

static NTSTATUS WINAPI
RtlUnicodeStringToAnsiString_UTF8(
                struct MS_ANSI_STRING * DestinationString,
                const struct MS_UNICODE_STRING * SourceString,
                BOOLEAN AllocateDestinationString)
{
	int cbRet;
#if DEBUG>=2
	PrintUnicodeString("IN:UNI", SourceString);
#endif
	if (AllocateDestinationString)
	{
		int cb = WideCharToMultiByte(CP_UTF8, 0,
		                                 SourceString->Buffer, SourceString->Length,
		                                 NULL, 0,
		                                 NULL, NULL);
		
		if (cb >= 0x10000) return 0x80000005; // STATUS_BUFFER_OVERFLOW
		if (cb+1 < 0x10000) cb += 1; // NUL terminator
		DestinationString->MaximumLength = cb;
		DestinationString->Buffer = (PCHAR)HeapAlloc(GetProcessHeap(), 0, DestinationString->MaximumLength+40);
		DestinationString->Buffer +=20;
	}
	
	cbRet = WideCharToMultiByte(CP_UTF8, 0,
	                                SourceString->Buffer, SourceString->Length/2,
	                                DestinationString->Buffer, DestinationString->MaximumLength,
	                                NULL, NULL);
	if (!cbRet || cbRet > DestinationString->MaximumLength) return 0x80000005; // STATUS_BUFFER_OVERFLOW
	if (cbRet < DestinationString->MaximumLength) DestinationString->Buffer[cbRet] = '\0';
	DestinationString->Length = cbRet;
#if DEBUG>=2
	PrintAnsiString("OUT:ANSI", DestinationString);
#endif
	return 0x00000000; // STATUS_SUCCESS
}


//https://sourceforge.net/p/predef/wiki/Architectures/
#if defined(_M_IX86) || defined(__i386__)
static void RedirectFunction_machine(LPBYTE victim, LPBYTE replacement)
{
	LONG_PTR ptrdiff = replacement-victim-5;
	
	victim[0] = 0xE9; // jmp (offset from next instruction)
	*(LONG_PTR*)(victim+1) = ptrdiff;
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
#if DEBUG>=1
printf("replacing %p with %p\n",victim,replacement);
#endif
	DWORD prot;
	// it's bad to have W+X on the same page, but I don't want to remove X from ntdll.dll.
	// if I hit NtProtectVirtualMemory, I won't be able to fix it
	VirtualProtect((void*)victim, 64, PAGE_EXECUTE_READWRITE, &prot);
	RedirectFunction_machine((LPBYTE)victim, (LPBYTE)replacement);
	VirtualProtect((void*)victim, 64, prot, &prot);
}

void WuTF_enable()
{
	HMODULE ntdll = GetModuleHandle("ntdll.dll");
	RedirectFunction(GetProcAddress(ntdll, "RtlAnsiStringToUnicodeString"), (FARPROC)RtlAnsiStringToUnicodeString_UTF8);
	RedirectFunction(GetProcAddress(ntdll, "RtlUnicodeStringToAnsiString"), (FARPROC)RtlUnicodeStringToAnsiString_UTF8);
	
	//possible extensions:
	//ntdll!RtlOemStringToUnicodeString, ntdll!RtlUnicodeStringToOemString
	// no notable known users
	//
	//ntdll!RtlMultiByteToUnicodeN,    ntdll!RtlUnicodeToMultiByteN,
	//ntdll!RtlMultiByteToUnicodeSize, ntdll!RtlUnicodeToMultiByteSize
	// used everywhere by Windows internals, including by RtlAnsiStringToUnicodeString itself
	//
	//ntdll!Rtl{Oem,Ansi}StringToUnicodeSize and reverse
	// no notable known users
	//
	//kernel32!MultiByteToWideChar(CP_ACP), CP_OEM, kernel32!WideCharToMultiByte
	// used by Wine msvcrt
	//  https://github.com/wine-mirror/wine/blob/e1970c8547aa7fed5a097faf172eadc282b3394e/dlls/msvcrt/file.c#L4070
	//  https://github.com/wine-mirror/wine/blob/e1970c8547aa7fed5a097faf172eadc282b3394e/dlls/msvcrt/file.c#L4042
	//  https://github.com/wine-mirror/wine/blob/e1970c8547aa7fed5a097faf172eadc282b3394e/dlls/msvcrt/data.c#L289
}


static LPSTR wcsdupa(LPCWSTR in)
{
	int cb = WideCharToMultiByte(CP_UTF8, 0, in, -1, NULL, 0, NULL, NULL);
	LPSTR ret = (LPSTR)HeapAlloc(GetProcessHeap(), 0, cb);
	WideCharToMultiByte(CP_UTF8, 0, in, -1, ret, cb, NULL, NULL);
	return ret;
}

void WuTF_args(int* argc_p, char** * argv_p)
{
	int i;
	
	int argc;
	LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
	LPSTR* argv = (LPSTR*)HeapAlloc(GetProcessHeap(), 0, sizeof(LPSTR)*(argc+1));
	
	for (i=0;i<argc;i++)
	{
		argv[i] = wcsdupa(wargv[i]);
	}
	argv[argc]=0;
	
	*argv_p = argv;
	*argc_p = argc;
	
	
}

#endif

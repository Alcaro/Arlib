#include "arlib.h"

#ifdef _WIN32
#define SMR "sm\xC3\xB6rg\xC3\xA5sr\xC3\xA4ka"
#define SMR_C "sm\x94rg\x86sr\x84ka"
#define SMR_W L"sm\x00F6rg\x00E5sr\x00E4ka"
#include <windows.h>

void testwindows(int argc, char * argv[])
{
	WuTF_enable_args(&argc, &argv);
	
	if (!argv[1])
	{
		puts("(1) SKIP, please invoke as: test(64).exe " SMR_C);
	}
	else if (!strcmp(argv[1], SMR))
	{
		puts("(1) PASS");
	}
	else
	{
		puts("(1) FAIL");
	}
	
	DWORD ignore;
	HANDLE h;
	h = CreateFileW(SMR_W L".txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
	WriteFile(h, "pokemon", 8, &ignore, NULL);
	CloseHandle(h);
	
	h = CreateFileA(SMR ".txt", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (h != INVALID_HANDLE_VALUE)
	{
		char p[8];
		ReadFile(h, p, 42, &ignore, NULL);
		if (!strcmp(p, "pokemon")) puts("(2) PASS");
		else puts("(2) FAIL: Wrong contents");
		CloseHandle(h);
	}
	else
	{
		printf("(2) FAIL: Couldn't open file (errno %lu)", GetLastError());
	}
	DeleteFileW(SMR_W L".txt");
	
	//this one takes two string arguments, one of which can be way longer than 260
#define PAD "Stretch string to 260 characters."
#define PAD2 PAD " " PAD
#define PAD8 PAD2 "\r\n" PAD2 "\r\n" PAD2 "\r\n" PAD2
	MessageBoxA(NULL, PAD8 "\r\n(4) CHECK: " SMR, "(3) CHECK: " SMR, MB_OK);
}
#endif


int main(int argc, char * argv[])
{
	void WuTF_utf16_test();
	WuTF_utf16_test();
#ifdef _WIN32
	testwindows(argc, argv);
#endif
}

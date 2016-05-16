//#include "arlib/socket.h"
#include "arlib/wutf.h"
#include <stdio.h>

int main(int argc, char * argv[])
{
	//void WuTF_utf16_test();
	//WuTF_utf16_test();
#ifdef _WIN32
	void WuTF_test_ansiutf();
	WuTF_test_ansiutf();
#endif
}

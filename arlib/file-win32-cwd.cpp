#include "file.h"

#ifdef _WIN32
#include <windows.h>
//separate file so this ctor can be optimized out if unused

namespace {
struct cwd_finder {
	string path;
	
	cwd_finder() // can't oninit() to initialize a string, globals' ctors' order is implementation defined and often wrong
	{
		char chars[PATH_MAX];
		GetCurrentDirectory(sizeof(chars), chars);
		char* iter = chars;
		while (*iter)
		{
			if (*iter == '\\') *iter = '/';
			iter++;
		}
		if (iter[-1] != '/') *iter++ = '/';
		path = cstring(bytesr((uint8_t*)chars, iter-chars));
	}
};

static cwd_finder g_cwd;
}

const string& file::cwd() { return g_cwd.path; }
#endif

#include "file.h"
#include "thread.h"

#ifdef _WIN32
//separate file so this oninit can be optimized out if unused

static char g_exepath[MAX_PATH];

#ifdef ARLIB_HYBRID_DLL
RUN_ONCE_FN(init)
#else
oninit()
#endif
{
	GetModuleFileName(NULL, g_exepath, MAX_PATH);
	for (int i=0;g_exepath[i];i++)
	{
		if (g_exepath[i]=='\\') g_exepath[i]='/';
	}
	char * end=strrchr(g_exepath, '/');
	if (end) end[1]='\0';
}

cstring file::exepath()
{
#ifdef ARLIB_HYBRID_DLL
	init();
#endif
	return g_exepath;
}
#endif

#pragma once
#include "global.h"

#ifdef ARLIB_TEST
class _testdecl {
public:
	_testdecl(bool(*func)(), const char * name);
};

#define test() \
	static bool _testfunc(); \
	static _testdecl _testdeclv(_testfunc, __FILE__); \
	static bool _testfunc()
#undef assert
#define assert(x) do { if (!(x)) { puts("\nFailed assertion " #x); return false; } } while(0)
#define assert_eq(x,y) do { \
		if ((x) != (y)) \
		{ \
			printf("\nFailed assertion " #x " == " #y " (line " STR(__LINE__) "): expected %s, got %s\n", (const char*)(y), (const char*)(x)); \
			return false; \
		} \
	} while(0)
#else
#define test() static bool MAYBE_UNUSED _testfunc()
#endif

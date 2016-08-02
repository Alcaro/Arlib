#pragma once
#include "global.h"
#include "stringconv.h"

#undef assert

#ifdef ARLIB_TEST

class _testdecl {
public:
	_testdecl(bool(*func)(), const char * name);
};

void _testeqfail(cstring name, cstring expected, cstring actual);

#define test() \
	static bool _testfunc##__LINE__(); \
	static _testdecl _testdeclv(_testfunc##__LINE__, __FILE__ ":" STR(__LINE__)); \
	static bool _testfunc##__LINE__()
#define assert(x) do { if (!(x)) { puts("\nFailed assertion " #x); return false; } } while(0)
#define assert_eq(x,y) do { \
		if ((x) != (y)) \
		{ \
			_testeqfail(#x " == " #y " (line " STR(__LINE__) ")", tostring(y), tostring(x)); \
			return false; \
		} \
	} while(0)

#else

#define test() static bool MAYBE_UNUSED JOIN(_testfunc_, __LINE__)()
#define assert(x)
#define assert_eq(x,y)

#endif

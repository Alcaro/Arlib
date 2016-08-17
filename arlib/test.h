#pragma once
#include "global.h"
#include "stringconv.h"

#undef assert

#ifdef ARLIB_TEST

class _testdecl {
public:
	_testdecl(void(*func)(), const char * name);
};

void _testfail(cstring why);
void _testeqfail(cstring name, cstring expected, cstring actual);

#define TESTFUNCNAME JOIN(_testfunc, __LINE__)
#define test() \
	static void TESTFUNCNAME(); \
	static _testdecl JOIN(_testdecl, __LINE__)(TESTFUNCNAME, __FILE__ ":" STR(__LINE__)); \
	static void TESTFUNCNAME()
#define assert(x) do { if (!(x)) { _testfail("\nFailed assertion " #x); return; } } while(0)
#define assert_eq(x,y) do { \
		if ((x) != (y)) \
		{ \
			_testeqfail(#x " == " #y " (line " STR(__LINE__) ")", tostring(y), tostring(x)); \
			return; \
		} \
	} while(0)

#else

#define test() static void MAYBE_UNUSED JOIN(_testfunc_, __LINE__)()
#define assert(x)
#define assert_eq(x,y)

#endif

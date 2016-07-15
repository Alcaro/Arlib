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
#else
#define test() static bool MAYBE_UNUSED _testfunc()
#endif

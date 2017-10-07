#pragma once
#include "global.h"
#include "stringconv.h"

#undef assert

#ifdef ARLIB_TEST

class _test_maybeptr {
	const char * data;
public:
	_test_maybeptr() : data(NULL) {}
	_test_maybeptr(const char * data) : data(data) {}
	
	operator const char *() { return data; }
};
class _testdecl {
public:
	_testdecl(void(*func)(), const char * loc, const char * name);
};

extern int _test_result;

void _testfail(cstring why, int line);
void _testcmpfail(cstring why, int line, cstring expected, cstring actual);

void _teststack_push(int line);
void _teststack_pop();

void _test_skip(cstring why);
void _test_skip_force_delay(cstring why);

template<typename T, typename T2>
bool _test_eq(const T& v, const T2& v2)
{
	return (v == v2);
}
//silence sign-comparison warning if lhs is size_t and rhs is integer constant
template<typename T>
bool _test_eq(const T& v, int v2)
{
	return (std::is_unsigned<T>::value || v2>=0) && (int)v == v2 && v == (T)v2;
}
//likewise, but for huge constants
template<typename T>
bool _test_eq(const T& v, long long v2)
{
	return (std::is_unsigned<T>::value || v2>=0) && (long long)v == v2 && v == (T)v2;
}

template<typename T, typename T2>
bool _test_lt(const T& v, const T2& v2)
{
	return (v < v2);
}

template<typename T, typename T2>
void _assert_eq(const T&  actual,   const char * actual_exp,
                const T2& expected, const char * expected_exp,
                int line)
{
	if (!_test_eq(actual, expected))
	{
		_testcmpfail((string)actual_exp+" == "+expected_exp, line, tostring(expected), tostring(actual));
	}
}

template<typename T, typename T2>
void _assert_lt(const T&  actual,   const char * actual_exp,
                const T2& expected, const char * expected_exp,
                int line)
{
	if (!_test_lt(actual, expected))
	{
		_testcmpfail((string)actual_exp+" < "+expected_exp, line, tostring(expected), tostring(actual));
	}
}

template<typename T, typename T2>
void _assert_lte(const T&  actual,   const char * actual_exp,
                 const T2& expected, const char * expected_exp,
                 int line)
{
	if (!!_test_lt(expected, actual)) // a<=b implemented as !(b<a)
	{
		_testcmpfail((string)actual_exp+" <= "+expected_exp, line, tostring(expected), tostring(actual));
	}
}

template<typename T, typename T2>
void _assert_range(const T&  actual, const char * actual_exp,
                   const T2& min,    const char * min_exp,
                   const T2& max,    const char * max_exp,
                   int line)
{
	if (_test_lt(actual, min) || _test_lt(max, actual))
	{
		_testcmpfail((string)actual_exp+" in ["+min_exp+".."+max_exp+"]", line, "["+tostring(min)+".."+tostring(max)+"]", tostring(actual));
	}
}

#define TESTFUNCNAME JOIN(_testfunc, __LINE__)
#define test(...) \
	static void TESTFUNCNAME(); \
	static KEEP_OBJECT _testdecl JOIN(_testdecl, __LINE__)(TESTFUNCNAME, __FILE__ ":" STR(__LINE__), _test_maybeptr(__VA_ARGS__)); \
	static void TESTFUNCNAME()
#define assert_ret(x, ret) do { if (!(x)) { _testfail("\nFailed assertion " #x, __LINE__); return ret; } } while(0)
#define assert(x) assert_ret(x,)
#define assert_msg_ret(x, msg, ret) do { if (!(x)) { _testfail((string)"\nFailed assertion " #x ": "+msg, __LINE__); return ret; } } while(0)
#define assert_msg(x, msg) assert_msg_ret(x,msg,)
#define assert_eq_ret(actual,expected,ret) do { \
		_assert_eq(actual, #actual, expected, #expected, __LINE__); \
		if (_test_result) return ret; \
	} while(0)
#define assert_eq(actual,expected) assert_eq_ret(actual,expected,)
#define assert_lt_ret(actual,expected,ret) do { \
		_assert_lt(actual, #actual, expected, #expected, __LINE__); \
		if (_test_result) return ret; \
	} while(0)
#define assert_lt(actual,expected) assert_lt_ret(actual,expected,)
#define assert_lte_ret(actual,expected,ret) do { \
		_assert_lte(actual, #actual, expected, #expected, __LINE__); \
		if (_test_result) return ret; \
	} while(0)
#define assert_lte(actual,expected) assert_lte_ret(actual,expected,)
#define assert_range_ret(actual,min,max,ret) do { \
		_assert_range(actual, #actual, min, #min, max, #max, __LINE__); \
		if (_test_result) return ret; \
	} while(0)
#define assert_range(actual,min,max) assert_range_ret(actual,min,max,)
#define assert_fail(msg) do { _testfail((string)"\n"+msg, __LINE__); return; } while(0)
#define assert_fail_nostack(msg) do { _testfail((string)"\n"+msg, -1); return; } while(0)
#define testcall(x) do { _teststack_push(__LINE__); x; _teststack_pop(); if (_test_result) return; } while(0)
#define test_skip(x) do { _test_skip(x); if (_test_result) return; } while(0)
#define test_skip_force_delay(x) do { _test_skip_force_delay(x); } while(0)

class runloop;
//This creates a specialized runloop that calls assert() if anything takes more than max_ms milliseconds.
//Note that running the program under Valgrind causes heavy latency penalties, up to 25ms. Don't set limit below 30ms.
runloop* runloop_blocktest_create(int max_ms = 1);

#else

#define test(...) static void MAYBE_UNUSED JOIN(_testfunc_, __LINE__)()
#define assert(x) ((void)(x))
#define assert_msg(x, msg) ((void)(x),(void)(msg))
#define assert_eq(x,y) ((void)(x==y))
#define assert_lt(x,y) ((void)(x<y))
#define assert_lte(x,y) ((void)(x<y))
#define assert_range(x,y,z) ((void)(x<y))
#define testcall(x) x
#define test_skip(x) return
#define test_skip_force(x) return

class runloop;
static inline runloop* runloop_blocktest_create() { return NULL; }

#endif

#pragma once
#include <type_traits>

#ifdef ARLIB_THREAD
//This header defines several functions for atomically operating on integers or pointers.
//You can use any builtin integer type, any typedef thereof, and any pointer (enum not allowed).
//The following functions exist:
//lock_read(T*)
//lock_write(T*, T)
//lock_incr(T*) (integers only, because adding 1 to a pointer is ambiguous)
//lock_decr(T*)
//lock_xchg(T*, T)
//lock_cmpxchg(T*, T, T)
//All of them use acquire-release ordering. If you know what you're doing, you can append _acq, _rel or _loose.
//(I have not been able to find any usecase for sequentially consistent ordering, or even any situation where it matters,
//  other than at least three threads doing something dubious with at least two variables.)

//All of these functions (except store) return the value before the operation.
//(cmp)xchg obviously does, so to ease memorization, the others do too.

//If doing cmpxchg on pointers and array indices, make sure you're not vulnerable to ABA problems.

#ifdef __GNUC__
//https://gcc.gnu.org/onlinedocs/gcc-4.7.0/gcc/_005f_005fatomic-Builtins.html
#define WITH_MODEL(fnname, model) \
	template<typename T> inline std::enable_if_t<std::is_integral_v<T>, T>                                      \
	lock_incr##fnname(T* val) { return __atomic_fetch_add(val, 1, model); }                                     \
	template<typename T> inline std::enable_if_t<std::is_integral_v<T>, T>                                      \
	lock_decr##fnname(T* val) { return __atomic_fetch_sub(val, 1, model); }                                     \
	template<typename T, typename T2> inline std::enable_if_t<std::is_integral_v<T> || std::is_pointer_v<T>, T> \
	lock_xchg##fnname(T* val, T2 newval) { return __atomic_exchange_n(val, newval, model); }                    \
	template<typename T, typename T2> inline std::enable_if_t<std::is_integral_v<T> || std::is_pointer_v<T>, T> \
	lock_cmpxchg##fnname(T* val, T2 old, T2 newval) {                                                           \
		__atomic_compare_exchange_n(val, &old, newval, false, model, __ATOMIC_RELAXED); return old; }
WITH_MODEL(, __ATOMIC_ACQ_REL)
WITH_MODEL(_acq, __ATOMIC_ACQUIRE)
WITH_MODEL(_rel, __ATOMIC_RELEASE)
WITH_MODEL(_loose, __ATOMIC_RELAXED)
#undef WITH_MODEL

#define lock_read_acq lock_read
template<typename T> inline std::enable_if_t<std::is_integral_v<T> || std::is_pointer_v<T>, T> \
lock_read(T* val) { return __atomic_load_n(val, __ATOMIC_ACQUIRE); }
template<typename T> inline std::enable_if_t<std::is_integral_v<T> || std::is_pointer_v<T>, T> \
lock_read_loose(T* val) { return __atomic_load_n(val, __ATOMIC_RELAXED); }

#define lock_write_rel lock_write
template<typename T, typename T2> inline std::enable_if_t<std::is_integral_v<T> || std::is_pointer_v<T>> \
lock_write(T* val, T2 newval) { return __atomic_store_n(val, newval, __ATOMIC_RELEASE); }
template<typename T, typename T2> inline std::enable_if_t<std::is_integral_v<T> || std::is_pointer_v<T>> \
lock_write_loose(T* val, T2 newval) { return __atomic_store_n(val, newval, __ATOMIC_RELAXED); }

#elif defined(_MSC_VER)
#include "../global.h"
#include <windows.h>

// not tested, I couldn't find a working MSVC
// in particular, need to check if MemoryBarrier() expands to dmb ish on ARM
#define WITH_MODEL(fnname, model)                                                                               \
	template<typename T> inline std::enable_if_t<std::is_integral_v<T>, T>                                      \
	lock_incr##fnname(T* val) { static_assert(sizeof(T)==4 || sizeof(T)==8);                                    \
		if (sizeof(T)==4) return InterlockedIncrement##model((volatile LONG*)val)-1;                            \
		if (sizeof(T)==8) return InterlockedIncrement##model##64((volatile LONG64*)val)-1; }                    \
	template<typename T> inline std::enable_if_t<std::is_integral_v<T>, T>                                      \
	lock_decr##fnname(T* val) { static_assert(sizeof(T)==4 || sizeof(T)==8);                                    \
		if (sizeof(T)==4) return InterlockedDecrement##model((volatile LONG*)val)+1;                            \
		if (sizeof(T)==8) return InterlockedDecrement##model##64((volatile LONG64*)val)+1; }                    \
	template<typename T, typename T2> inline std::enable_if_t<std::is_integral_v<T> || std::is_pointer_v<T>, T> \
	lock_xchg##fnname(T* val, T2 newval) { static_assert(sizeof(T)==4 || sizeof(T)==8);                         \
		if (sizeof(T)==4) return InterlockedExchange##model((volatile LONG*)val, newval);                       \
		if (sizeof(T)==8) return InterlockedExchange##model##64((volatile LONG64*)val, newval); }               \
	template<typename T, typename T2> inline std::enable_if_t<std::is_integral_v<T> || std::is_pointer_v<T>, T> \
	lock_cmpxchg##fnname(T* val, T2 old, T2 newval) { static_assert(sizeof(T)==4 || sizeof(T)==8);              \
		if (sizeof(T)==4) return InterlockedCompareExchange##model((volatile LONG*)val, old, newval);           \
		if (sizeof(T)==8) return InterlockedCompareExchange##model##64((volatile LONG64*)val, old, newval); }
WITH_MODEL(,)
WITH_MODEL(_acq,Acquire)
WITH_MODEL(_rel,Release)
WITH_MODEL(_loose,NoFence)
#undef WITH_MODEL

// in older MSVC, volatile access was guaranteed to always fence; in newer, that's deprecated, and off by default on ARM (/volatile:ms)
// instead, let's rely on https://docs.microsoft.com/en-us/windows/win32/sync/interlocked-variable-access, which says
// "Simple reads and writes to properly-aligned 32-bit variables are atomic operations." (as of september 2020)
// however, that's only NoTear / Unordered atomic; lock_read_loose needs at least Relaxed / Monotonic, so more manual fences
// it's unambiguously a race condition and UB per the C++ standard, hope MSVC doesn't try to loosen that guarantee on future platforms...
template<typename T> inline std::enable_if_t<std::is_integral_v<T> || std::is_pointer_v<T>, T>
lock_read(T* val) { static_assert(sizeof(T)==4 || sizeof(T)==8); static_assert(sizeof(T) <= sizeof(void*));
	T ret = *val; MemoryBarrier(); return ret; }
#define lock_read_acq lock_read
#define lock_read_loose lock_read

template<typename T> inline std::enable_if_t<std::is_integral_v<T> || std::is_pointer_v<T>>
lock_write(T* val, T newval) { static_assert(sizeof(T)==4 || sizeof(T)==8); static_assert(sizeof(T) <= sizeof(void*));
	MemoryBarrier(); *val = newval; }
#define lock_write_rel lock_write
#define lock_write_loose lock_write

#endif

#else

#define WITH_MODEL(fnname, model) \
	template<typename T> T lock_read##fnname(T* val) { return *val; } \
	template<typename T, typename T2> void lock_write##fnname(T* val, T2 newval) { *val = newval; } \
	template<typename T, typename T2> T lock_cmpxchg##fnname(T* val, T2 old, T2 newval) \
		{ T ret = *val; if (*val == old) *val = newval; return ret; } \
	template<typename T, typename T2> T lock_xchg##fnname(T* val, T2 newval) { T ret = *val; *val = newval; return ret; } \
	template<typename T> T lock_incr##fnname(T* val) { return (*val)++; } \
	template<typename T> T lock_decr##fnname(T* val) { return (*val)--; } \
//if single threaded, all consistency models are identical
WITH_MODEL(,)
WITH_MODEL(_acq,)
WITH_MODEL(_rel,)
WITH_MODEL(_loose,)
#undef WITH_MODEL

#endif

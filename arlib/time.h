#pragma once
#include "global.h"
#include <compare>
#include <type_traits>
#include <time.h>

#ifdef _WIN32
typedef struct _FILETIME FILETIME;
#endif

struct duration {
	time_t sec;
	long nsec; // to match timestamp
	
	static duration ms(int ms) { return { ms/1000, ms%1000*1000000 }; }
};
struct timestamp {
	time_t sec;
	long nsec; // disgusting type, but struct timespec is time_t + long, and I want to be binary compatible
	
	std::strong_ordering operator<=>(const timestamp& other) const = default;
	timestamp operator+(duration dur) const
	{
		timestamp ret;
		ret.sec += dur.sec;
		ret.nsec += dur.nsec;
		if (ret.nsec > 1000000000)
		{
			ret.sec++;
			ret.nsec -= 1000000000;
		}
		return ret;
	}
	
#ifdef __unix__
	static timestamp from_native(struct timespec ts) { return *(timestamp*)&ts; }
	struct timespec to_native() const { return *(struct timespec*)this; }
#else
	static timestamp from_native(FILETIME ts);
	FILETIME to_native() const;
#endif
	
#ifdef __unix__
	static timestamp now()
	{
#if __GNUC__ >= 12 || __clang_major__ >= 99 // not supported yet
		static_assert(std::is_layout_compatible_v<struct timespec, timestamp>);
#endif
		timestamp ret;
		clock_gettime(CLOCK_MONOTONIC, (struct timespec*)&ret);
		return ret;
	}
#else
	static timestamp now();
#endif
	
	// Returns a timestamp in the far future, suitable as timeout for infinite waits.
	// This exact timestamp may be hardcoded; don't do any math on it.
	static timestamp at_never() { return { sec_never(), 0 }; }
	
	static time_t sec_never()
	{
		if (sizeof(time_t) == sizeof(int32_t)) return INT32_MAX;
		if (sizeof(time_t) == sizeof(int64_t)) return INT64_MAX;
		__builtin_trap();
	}
};

#pragma once
#include "global.h"
#include <compare>
#include <time.h>
#include <type_traits>

#ifdef _WIN32
typedef struct _FILETIME FILETIME;
#endif

class cstring;
class string;

struct duration {
	time_t sec = 0;
	long nsec = 0; // to match timestamp
	
	std::strong_ordering operator<=>(const duration& other) const = default;
	
	static duration ms(int ms) { return { ms/1000, ms%1000*1000000 }; }
	int ms() const { return sec*1000 + nsec/1000000; }
	int64_t us() const { return sec*1000000 + nsec/1000; }
};
struct timestamp {
	time_t sec;
	long nsec; // disgusting type, but struct timespec is time_t + long, and I want to be binary compatible
	
	std::strong_ordering operator<=>(const timestamp& other) const = default;
	timestamp operator+(duration dur) const
	{
		timestamp ret = *this;
		ret.sec += dur.sec;
		ret.nsec += dur.nsec;
		if (__builtin_constant_p(dur.nsec) && dur.nsec == 0) {}
		else if (ret.nsec > 1000000000)
		{
			ret.sec++;
			ret.nsec -= 1000000000;
		}
		return ret;
	}
	
	timestamp operator-(duration other) const
	{
		timestamp ret;
		ret.sec = sec - other.sec;
		ret.nsec = nsec - other.nsec;
		if (__builtin_constant_p(other.nsec) && other.nsec == 0) {}
		else if (ret.nsec < 0)
		{
			ret.sec--;
			ret.nsec += 1000000000;
		}
		return ret;
	}
	duration operator-(timestamp other) const
	{
		duration ret;
		ret.sec = sec - other.sec;
		ret.nsec = nsec - other.nsec;
		if (ret.nsec < 0)
		{
			ret.sec--;
			ret.nsec += 1000000000;
		}
		return ret;
	}
	
#ifdef __unix__
	static timestamp from_native(struct timespec ts) { return transmute<timestamp>(ts); }
	struct timespec to_native() const { return transmute<struct timespec>(*this); }
#else
	static timestamp from_native(FILETIME ts);
	FILETIME to_native() const;
#endif
	
#ifdef __unix__
	static timestamp now()
	{
// TODO: remove this ifdef when dropping the offending compilers
#if (defined(__clang_major__) && __clang_major__ >= 18) || (!defined(__clang_major__) && defined(__GNUC__) && __GNUC__ >= 12)
		static_assert(std::is_layout_compatible_v<struct timespec, timestamp>);
#endif
		timestamp ret;
		clock_gettime(CLOCK_REALTIME, (struct timespec*)&ret);
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
		if constexpr (sizeof(time_t) == sizeof(int32_t)) return INT32_MAX;
		if constexpr (sizeof(time_t) == sizeof(int64_t)) return INT64_MAX;
		__builtin_trap();
	}
	
	static timestamp in_sec(time_t sec)
	{
		return now() + duration{sec, 0};
	}
	static timestamp in_ms(int ms)
	{
		return now() + duration::ms(ms);
	}
	static timestamp from_iso8601(cstring stamp);
	
	using serialize_as = string;
};

bool fromstring(cstring s, timestamp& out);
string tostring(timestamp val);

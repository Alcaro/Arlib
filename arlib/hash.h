#pragma once
#include "global.h"
#include "array.h"

// Hash values are guaranteed stable within the process, but nothing else. Do not persist them outside the process.
// They are allowed to change along with the build target, Arlib version, build time, kernel version, etc.
// They are only expected to be unique, not high entropy; entropy can be improved with hash_shuffle.
// Don't rely on them for any security-related purpose either.

template<typename T>
auto hash(T val) requires (std::is_integral_v<T>)
{
	if constexpr (sizeof(T) == 8)
		return (uint64_t)val;
	else
		return (uint32_t)val;
}
template<typename T>
auto hash(const T& val) requires requires { val.hash(); }
{
	return val.hash();
}
size_t hash(const uint8_t * val, size_t n);
static inline size_t hash(const char * val)
{
	return hash((uint8_t*)val, strlen(val));
}
static inline size_t hash(const bytesr& val)
{
	return hash(val.ptr(), val.size());
}
static inline size_t hash(const bytesw& val)
{
	return hash(val.ptr(), val.size());
}
static inline size_t hash(const bytearray& val)
{
	return hash(val.ptr(), val.size());
}

class pointer_hasher {
	pointer_hasher() = delete;
public:
	template<typename T>
	static size_t hash(T* ptr) { return (uintptr_t)ptr; }
};
class arrayview_hasher {
	arrayview_hasher() = delete;
public:
	template<typename T>
	static size_t hash(arrayview<T> arr) requires (std::is_integral_v<T>) { return ::hash(arr.template transmute<uint8_t>()); }
};


// the regular ones give weak entropy in the higher bits; the strong ones give high entropy everywhere
inline uint32_t hash_shuffle(uint32_t val) { return __builtin_bswap32(val * 1086221891); } // just a random prime
inline uint64_t hash_shuffle(uint64_t val) { return __builtin_bswap64(val * 8040991081842494123); }
inline uint32_t hash_shuffle_strong(uint32_t val)
{
	//https://code.google.com/p/smhasher/wiki/MurmurHash3
	val ^= val >> 16;
	val *= 0x85ebca6b;
	val ^= val >> 13;
	val *= 0xc2b2ae35;
	val ^= val >> 16;
	return val;
}
inline uint64_t hash_shuffle_strong(uint64_t val)
{
	//http://zimbry.blogspot.se/2011/09/better-bit-mixing-improving-on.html Mix13
	val ^= val >> 30;
	val *= 0xbf58476d1ce4e5b9;
	val ^= val >> 27;
	val *= 0x94d049bb133111eb;
	val ^= val >> 31;
	return val;
}
// nothing uses these, but why not
inline uint32_t hash_shuffle_inv(uint32_t val) { return __builtin_bswap32(val) * 498781803; } // multiplicative inverse mod 2**32
inline uint64_t hash_shuffle_inv(uint64_t val) { return __builtin_bswap64(val) * 4738224585390907395; }
inline uint32_t hash_shuffle_strong_inv(uint32_t val)
{
	val ^= val >> 16;
	val *= 0x7ed1b41d;
	val ^= val >> 13; val ^= val >> 26;
	val *= 0xa5cb9243;
	val ^= val >> 16;
	return val;
}
inline uint64_t hash_shuffle_strong_inv(uint64_t val)
{
	val ^= val >> 31; val ^= val >> 62;
	val *= 0x319642b2d24d8ec3;
	val ^= val >> 27; val ^= val >> 54;
	val *= 0x96de1b173f119089;
	val ^= val >> 30; val ^= val >> 60;
	return val;
}

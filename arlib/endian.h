#pragma once
#include "global.h"
#include "intwrap.h"
#include "array.h"
#include <stdint.h>

//This one defines:
//Macros LSB_FIRST and MSB_FIRST
//  Exactly one is defined, depending on your hardware.
//Macros LSB_FIRST_V and MSB_FIRST_V
//  Defined to 1 if {L,M}SB_FIRST is defined, otherwise 0. Separate from the above for libretro-common compatibility.
//end_swap{8,16,32,64}()
//  Byteswaps an integer. The 8 version just returns its input unchanged.
//end_swap()
//  Calls the appropriate end_swapN depending on argument size.
//end_{le,be,nat}{,8,16,32,64}_to_{le,be,nat}() (if exactly one le/be/nat is 'nat')
//  Byteswaps an integer or returns it unmodified, depending on the host endianness.
//  end_natN_to_le() is identical to end_leN_to_nat(). It's the best name I could think of.
//  The bit count is optional, and checks the input type if absent.
//Class litend<> and bigend<>
//  Acts like the given integer type, but is stored under the named endianness internally.
//  Intended to be used for parsing files via struct overlay, or for cross-platform structure passing.
//  Therefore, it has no padding, and is safe to memcpy() and fwrite().

// first line is for GCC 4.6 and Clang 3.2, second is for MSVC
#if (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) || \
    defined(_M_IX86) || defined(_M_AMD64) || defined(_M_ARM) || defined(_M_ARM64)
# define LSB_FIRST
#elif (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__) || \
    defined(_M_PPC)
# define MSB_FIRST
#else
// MSVC can run on _M_ALPHA and _M_IA64 too, but they're both bi-endian; need to find what mode MSVC runs them at
# error "unknown platform, can't determine endianness"
#endif

#ifdef LSB_FIRST
# define LSB_FIRST_V 1
#else
# define LSB_FIRST_V 0
#endif
#ifdef MSB_FIRST
# define MSB_FIRST_V 1
#else
# define MSB_FIRST_V 0
#endif

static inline uint8_t end_swap8(uint8_t n) { return n; }
#if defined(__GNUC__)
//This one is mostly useless (GCC detects the pattern and optimizes it).
//However, MSVC doesn't, so I need the intrinsics. Might as well use both sets.
static inline uint16_t end_swap16(uint16_t n) { return __builtin_bswap16(n); }
static inline uint32_t end_swap32(uint32_t n) { return __builtin_bswap32(n); }
static inline uint64_t end_swap64(uint64_t n) { return __builtin_bswap64(n); }
#elif defined(_MSC_VER)
static inline uint16_t end_swap16(uint16_t n) { return _byteswap_ushort(n); }
static inline uint32_t end_swap32(uint32_t n) { return _byteswap_ulong(n); }
static inline uint64_t end_swap64(uint64_t n) { return _byteswap_uint64(n); }
#else
static inline uint16_t end_swap16(uint16_t n) { return n>>8 | n<<8; }
static inline uint32_t end_swap32(uint32_t n)
{
	n = n>>16 | n<<16;
	n = (n&0x00FF00FF)<<8 | (n&0xFF00FF00)>>8;
	return n;
}
static inline uint64_t end_swap64(uint64_t n)
{
	n = n>>32 | n<<32;
	n = (n&0x0000FFFF0000FFFF)<<16 | (n&0xFFFF0000FFFF0000)>>16;
	n = (n&0x00FF00FF00FF00FF)<<8  | (n&0xFF00FF00FF00FF00)>>8 ;
	return n;
}
#endif

static inline uint8_t  end_swap(uint8_t  n) { return end_swap8(n);  }
static inline uint16_t end_swap(uint16_t n) { return end_swap16(n); }
static inline uint32_t end_swap(uint32_t n) { return end_swap32(n); }
static inline uint64_t end_swap(uint64_t n) { return end_swap64(n); }
static inline int8_t  end_swap(int8_t  n) { return (int8_t )end_swap((uint8_t )n); }
static inline int16_t end_swap(int16_t n) { return (int16_t)end_swap((uint16_t)n); }
static inline int32_t end_swap(int32_t n) { return (int32_t)end_swap((uint32_t)n); }
static inline int64_t end_swap(int64_t n) { return (int64_t)end_swap((uint64_t)n); }

#if defined(LSB_FIRST)
template<typename T> static inline T end_nat_to_le(T val) { return val; }
template<typename T> static inline T end_nat_to_be(T val) { return end_swap(val); }
template<typename T> static inline T end_le_to_nat(T val) { return val; }
template<typename T> static inline T end_be_to_nat(T val) { return end_swap(val); }
#elif defined(MSB_FIRST)
template<typename T> static inline T end_nat_to_le(T val) { return end_swap(val); }
template<typename T> static inline T end_nat_to_be(T val) { return val; }
template<typename T> static inline T end_le_to_nat(T val) { return end_swap(val); }
template<typename T> static inline T end_be_to_nat(T val) { return val; }
#endif

static inline uint8_t end_nat8_to_le(uint8_t val) { return val; }
static inline uint8_t end_nat8_to_be(uint8_t val) { return val; }
static inline uint8_t end_le8_to_nat(uint8_t val) { return val; }
static inline uint8_t end_be8_to_nat(uint8_t val) { return val; }
static inline uint16_t end_nat16_to_le(uint16_t val) { return end_nat_to_le(val); }
static inline uint16_t end_nat16_to_be(uint16_t val) { return end_nat_to_be(val); }
static inline uint16_t end_le16_to_nat(uint16_t val) { return end_le_to_nat(val); }
static inline uint16_t end_be16_to_nat(uint16_t val) { return end_be_to_nat(val); }
static inline uint32_t end_nat32_to_le(uint32_t val) { return end_nat_to_le(val); }
static inline uint32_t end_nat32_to_be(uint32_t val) { return end_nat_to_be(val); }
static inline uint32_t end_le32_to_nat(uint32_t val) { return end_le_to_nat(val); }
static inline uint32_t end_be32_to_nat(uint32_t val) { return end_be_to_nat(val); }
static inline uint64_t end_nat64_to_le(uint64_t val) { return end_nat_to_le(val); }
static inline uint64_t end_nat64_to_be(uint64_t val) { return end_nat_to_be(val); }
static inline uint64_t end_le64_to_nat(uint64_t val) { return end_le_to_nat(val); }
static inline uint64_t end_be64_to_nat(uint64_t val) { return end_be_to_nat(val); }

//read unaligned
inline uint8_t  readu_le8( const uint8_t* in) { uint8_t  ret; memcpy(&ret, in, sizeof(ret)); return end_nat_to_le(ret); }
inline uint8_t  readu_be8( const uint8_t* in) { uint8_t  ret; memcpy(&ret, in, sizeof(ret)); return end_nat_to_be(ret); }
inline uint16_t readu_le16(const uint8_t* in) { uint16_t ret; memcpy(&ret, in, sizeof(ret)); return end_nat_to_le(ret); }
inline uint16_t readu_be16(const uint8_t* in) { uint16_t ret; memcpy(&ret, in, sizeof(ret)); return end_nat_to_be(ret); }
inline uint32_t readu_le32(const uint8_t* in) { uint32_t ret; memcpy(&ret, in, sizeof(ret)); return end_nat_to_le(ret); }
inline uint32_t readu_be32(const uint8_t* in) { uint32_t ret; memcpy(&ret, in, sizeof(ret)); return end_nat_to_be(ret); }
inline uint64_t readu_le64(const uint8_t* in) { uint64_t ret; memcpy(&ret, in, sizeof(ret)); return end_nat_to_le(ret); }
inline uint64_t readu_be64(const uint8_t* in) { uint64_t ret; memcpy(&ret, in, sizeof(ret)); return end_nat_to_be(ret); }
inline uint8_t  readu_le8( arrayview<uint8_t> in) { return readu_le8( in.ptr()); }
inline uint8_t  readu_be8( arrayview<uint8_t> in) { return readu_be8( in.ptr()); }
inline uint16_t readu_le16(arrayview<uint8_t> in) { return readu_le16(in.ptr()); }
inline uint16_t readu_be16(arrayview<uint8_t> in) { return readu_be16(in.ptr()); }
inline uint32_t readu_le32(arrayview<uint8_t> in) { return readu_le32(in.ptr()); }
inline uint32_t readu_be32(arrayview<uint8_t> in) { return readu_be32(in.ptr()); }
inline uint64_t readu_le64(arrayview<uint8_t> in) { return readu_le64(in.ptr()); }
inline uint64_t readu_be64(arrayview<uint8_t> in) { return readu_be64(in.ptr()); }

inline sarray<uint8_t,1> pack_le8( uint8_t  n) { n = end_nat_to_le(n); sarray<uint8_t,1> ret; memcpy(ret.ptr(), &n, 1); return ret; }
inline sarray<uint8_t,1> pack_be8( uint8_t  n) { n = end_nat_to_be(n); sarray<uint8_t,1> ret; memcpy(ret.ptr(), &n, 1); return ret; }
inline sarray<uint8_t,2> pack_le16(uint16_t n) { n = end_nat_to_le(n); sarray<uint8_t,2> ret; memcpy(ret.ptr(), &n, 2); return ret; }
inline sarray<uint8_t,2> pack_be16(uint16_t n) { n = end_nat_to_be(n); sarray<uint8_t,2> ret; memcpy(ret.ptr(), &n, 2); return ret; }
inline sarray<uint8_t,4> pack_le32(uint32_t n) { n = end_nat_to_le(n); sarray<uint8_t,4> ret; memcpy(ret.ptr(), &n, 4); return ret; }
inline sarray<uint8_t,4> pack_be32(uint32_t n) { n = end_nat_to_be(n); sarray<uint8_t,4> ret; memcpy(ret.ptr(), &n, 4); return ret; }
inline sarray<uint8_t,8> pack_le64(uint64_t n) { n = end_nat_to_le(n); sarray<uint8_t,8> ret; memcpy(ret.ptr(), &n, 8); return ret; }
inline sarray<uint8_t,8> pack_be64(uint64_t n) { n = end_nat_to_be(n); sarray<uint8_t,8> ret; memcpy(ret.ptr(), &n, 8); return ret; }

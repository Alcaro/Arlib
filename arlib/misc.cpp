#include "global.h"
#include "endian.h"
#include "hash.h"
#include "simd.h"
#include "stringconv.h"
#include "os.h"
#include "test.h"
#include <new>

// trigger a warning if it doesn't stay disabled
#define __USE_MINGW_ANSI_STDIO 0


static void malloc_fail(size_t size)
{
	char buf[64];
	sprintf(buf, "malloc failed, size %" PRIuPTR "\n", size);
	debug_fatal(buf);
}

void* xmalloc_inner(size_t size)
{
	_test_malloc();
	if (size >= 0x80000000) malloc_fail(size);
	void* ret = try_malloc(size);
	if (size && !ret) malloc_fail(size);
	return ret;
}
void* xrealloc_inner(void* ptr, size_t size)
{
	if ((void*)ptr) _test_free();
	if (size) _test_malloc();
	if (size >= 0x80000000) malloc_fail(size);
	void* ret = try_realloc(ptr, size);
	if (size && !ret) malloc_fail(size);
	return ret;
}
void* xcalloc_inner(size_t size, size_t count)
{
	_test_malloc();
	void* ret = try_calloc(size, count);
	if (size && count && !ret) malloc_fail(size*count);
	return ret;
}

// Loads len bytes from the pointer, native endian. The high part is zero. load_small<uint64_t>("\x11\x22\x33", 3) is 0x112233 or 0x332211.
// On little endian, equivalent to T ret = 0; memcpy(&ret, ptr, len);, but faster.
// T must be an unsigned builtin integer type, and len must be <= sizeof(T) and >= 1.
template<typename T>
T load_small(const uint8_t * ptr, size_t len)
{
	static_assert(std::is_unsigned_v<T>);
#if !defined(ARLIB_OPT)
	if (RUNNING_ON_VALGRIND)
	{
		// like memmem.cpp load_sse2_small_highundef, Valgrind does not like the below one
		T ret = 0;
		memcpy(&ret, ptr, len);
#if END_BIG
		if (len == 0)
			return 0;
		ret >>= (sizeof(T)-len)*8;
#endif
		return ret;
	}
#endif
	
	if (uintptr_t(ptr) & sizeof(T))
	{
		// if the sizeof(T) bit is set, then extending the read upwards could potentially hit the next page
		// but extending downwards is safe, so do that
		T ret;
		memcpy(&ret, ptr-sizeof(T)+len, sizeof(T));
#if END_LITTLE
		ret >>= (sizeof(T)-len)*8;
#else
		ret &= ~(((T)-2) << (len*8-1)); // extra -1 on shift, and -2 on lhs, to avoid trouble if len == sizeof
#endif
		return ret;
	}
	else
	{
		// if the sizeof(T) bit is not set, then extending downwards could hit previous page, but extending upwards is safe
		// (in both cases, alignment is required)
		T ret;
		memcpy(&ret, ptr, sizeof(T));
#if END_LITTLE
		ret &= ~(((T)-2) << (len*8-1));
#else
		ret >>= (sizeof(T)-len)*8;
#endif
		return ret;
	}
}

#ifdef runtime__AES__
#include <immintrin.h>
#endif

size_t hash(const uint8_t * val, size_t n)
{
	if (n == 0)
		return 0;
	
#ifdef runtime__SSE2__
	if (runtime__SSE2__ && sizeof(size_t) == 8 && true)
	{
		auto digest1616_16 = [](__m128i a, __m128i b)
		{
			return _mm_xor_si128(_mm_madd_epi16(a, _mm_set1_epi32(0xF7D3857B)), b); // two random 16bit primes, concatenated
		};
		auto digest16_8 = [](__m128i a)
		{
			return _mm_cvtsi128_si64(_mm_sub_epi64(a, _mm_shuffle_epi32(a, _MM_SHUFFLE(0,1,2,3))));
		};
		
		if (n < 16)
		{
			__m128i ret = _mm_loadu_si128_highzero((__m128i*)val, n);
			return digest16_8(ret);
		}
#ifdef runtime__AES__
		if (runtime__AES__ && n >= 16)
		{
			auto hash_sseaes = [](const uint8_t * val, size_t n) __attribute__((target("aes"))) -> size_t
			{
				auto digest1616_16 = [](__m128i a, __m128i b) __attribute__((target("aes"), always_inline))
				{
					// I don't care about AES in particular, but it's faster than most other two-input insns
					return _mm_aesdec_si128(a, b);
				};
				auto digest16_8 = [](__m128i a) __attribute__((target("aes"), always_inline))
				{
					a = _mm_aesenc_si128(a, a); // AES output bytes only depend on a few input bytes
					a = _mm_aesenc_si128(a, a); // run a few more rounds so it diffuses properly
					return _mm_cvtsi128_si64(a);
				};
				__m128i ret = _mm_setzero_si128();
				while (n > 16)
				{
					ret = digest1616_16(ret, _mm_loadu_si128((__m128i*)val));
					val += 16;
					n -= 16;
				}
				val -= 16-n;
				ret = digest1616_16(ret, _mm_loadu_si128((__m128i*)val));
				return digest16_8(ret);
			};
			return hash_sseaes(val, n);
		}
#endif
		__m128i ret = _mm_setzero_si128();
		while (n > 16)
		{
			ret = digest1616_16(ret, _mm_loadu_si128((__m128i*)val));
			val += 16;
			n -= 16;
		}
		val -= 16-n;
		ret = digest1616_16(ret, _mm_loadu_si128((__m128i*)val));
		return digest16_8(ret);
	}
#endif
	
	if (n < sizeof(size_t))
		return load_small<size_t>(val, n);
	
	size_t hash = 5381;
	while (n > sizeof(size_t))
	{
		size_t tmp;
		memcpy(&tmp, val, sizeof(size_t));
		if constexpr (sizeof(size_t) == 8)
			hash = (__builtin_bswap64(hash)^tmp) * 0xca7c2633f12ae399; // extra swap because otherwise bottom byte of output would
		else                                                           //  only be affected by every 8th byte of input
			hash = (__builtin_bswap32(hash)^tmp) * 0x97c50251;         // the numbers are just random primes above SIZE_MAX/2
		val += sizeof(size_t);
		n -= sizeof(size_t);
	}
	
	// do a final size_t load overlapping with the previous bytes (or not overlapping, if n is a multiple of sizeof(size_t))
	val -= (sizeof(size_t)-n);
	size_t tmp;
	memcpy(&tmp, val, sizeof(size_t));
	
	return hash + tmp;
}


#ifdef runtime__SSE2__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpsabi" // doesn't work, but...
#define SIMD_DEBUG_INNER(suffix, sse_type, inner_type, fmt) \
	void debug##suffix(const sse_type& vals) \
	{ \
		inner_type inner[sizeof(sse_type)/sizeof(inner_type)]; \
		memcpy(inner, &vals, sizeof(sse_type)); \
		for (size_t i : range(ARRAY_SIZE(inner))) \
			printf("%s%c", (const char*)fmt(inner[i]), (i == ARRAY_SIZE(inner)-1 ? '\n' : ' ')); \
	} \
	void debug##suffix(const char * prefix, const sse_type& vals) { printf("%s ", prefix); debug##suffix(vals); }
#define SIMD_DEBUG_OUTER(bits) \
	SIMD_DEBUG_INNER(d##bits, __m128i, int##bits##_t, tostring) \
	SIMD_DEBUG_INNER(u##bits, __m128i, uint##bits##_t, tostring) \
	SIMD_DEBUG_INNER(x##bits, __m128i, uint##bits##_t, tostringhex<bits/4>)
SIMD_DEBUG_OUTER(8)
SIMD_DEBUG_OUTER(16)
SIMD_DEBUG_OUTER(32)
SIMD_DEBUG_OUTER(64)
SIMD_DEBUG_INNER(f32, __m128, float, tostring)
SIMD_DEBUG_INNER(f64, __m128d, double, tostring)
#undef SIMD_DEBUG_INNER
#undef SIMD_DEBUG_OUTER
void debugc8(const __m128i& vals)
{
	char inner[sizeof(__m128i)/sizeof(char)];
	memcpy(inner, &vals, sizeof(__m128i));
	for (size_t i : range(ARRAY_SIZE(inner)))
		printf("%c%c", inner[i], (i == ARRAY_SIZE(inner)-1 ? '\n' : ' '));
}
void debugc8(const char * prefix, const __m128i& vals) { printf("%s ", prefix); debugc8(vals); }
#pragma GCC diagnostic pop
#endif

//for windows:
//  these are the only libstdc++ functions I use. if I reimplement them, I don't need that library at all,
//   saving several hundred kilobytes and a DLL
//  (doesn't matter on linux where libstdc++ already exists)
//for tests:
//  need to override them, so they can be counted, leak checked, and rejected in test_nomalloc
//  Valgrind overrides my new/delete override with an LD_PRELOAD, but Valgrind has its own leak checker,
//   and new is only used for classes with vtables that don't make sense to use in test_nomalloc anyways
//  (I can confuse and disable valgrind's override by compiling with -s, but that has the obvious side effects.)
#if defined(__MINGW32__) || defined(ARLIB_TESTRUNNER)
void* operator new(std::size_t n) _GLIBCXX_THROW(std::bad_alloc) { return try_malloc(n); }
//Valgrind 3.13 overrides operator delete(void*), but not delete(void*,size_t)
//do not inline into free(p) until valgrind is fixed
#ifndef __MINGW32__ // mingw marks it inline, which obviously can't be used with noinline, but mingw doesn't need valgrind workarounds
__attribute__((noinline))
#endif
void operator delete(void* p) noexcept { free(p); }
#if __cplusplus >= 201402
void operator delete(void* p, std::size_t n) noexcept { operator delete(p); }
#endif
#endif

#ifdef __MINGW32__
extern "C" void __cxa_pure_virtual(); // predeclaration for -Wmissing-declarations
extern "C" void __cxa_pure_virtual() { __builtin_trap(); }

// pseudo relocs are disabled, and its handler takes ~1KB; better stub it out
extern "C" void _pei386_runtime_relocator();
extern "C" void _pei386_runtime_relocator() {}
#endif

// don't know where to put this, and nothing uses it anyways
// may return weird results if there is no modular inverse, i.e. gcd(a,b) != 1 (happens if b != 1 when a = 0)
template<typename T>
T modular_inv_inner(T a, T b, T c, T x, T y)
{
	if (a != 0)
		return modular_inv_inner(b%a, a, c, y, x-y*(b/a));
	if (x > c) // not interested in the negative result
		return c+x;
	return x;
}
template<typename T, typename T2>
auto modular_inv(T a, T2 b)
{
	return modular_inv_inner<std::make_unsigned_t<decltype(a+b)>>(a, b, b, 0, 1);
}
template<typename T>
auto modular_inv(T a) // modulo the number of unique values in T
{
	using T2 = std::make_unsigned_t<T>;
	T2 a2 = a;
	return modular_inv_inner<T2>(-a2%a2, a2, 0, 1, -(-a2/a2)-1);
}


test("bitround", "", "")
{
	assert_eq(bitround((unsigned)0), 1);
	assert_eq(bitround((unsigned)1), 1);
	assert_eq(bitround((unsigned)2), 2);
	assert_eq(bitround((unsigned)3), 4);
	assert_eq(bitround((unsigned)4), 4);
	assert_eq(bitround((unsigned)640), 1024);
	assert_eq(bitround((unsigned)0x7FFFFFFF), 0x80000000);
	assert_eq(bitround((unsigned)0x80000000), 0x80000000);
	assert_eq(bitround((signed)0), 1);
	assert_eq(bitround((signed)1), 1);
	assert_eq(bitround((signed)2), 2);
	assert_eq(bitround((signed)3), 4);
	assert_eq(bitround((signed)4), 4);
	assert_eq(bitround((signed)640), 1024);
	assert_eq(bitround((signed)0x3FFFFFFF), 0x40000000);
	assert_eq(bitround((signed)0x40000000), 0x40000000);
	assert_eq(bitround<uint8_t>(0), 1);
	assert_eq(bitround<uint8_t>(1), 1);
	assert_eq(bitround<uint8_t>(2), 2);
	assert_eq(bitround<uint8_t>(3), 4);
	assert_eq(bitround<uint8_t>(4), 4);
	assert_eq(bitround<uint16_t>(0), 1);
	assert_eq(bitround<uint16_t>(1), 1);
	assert_eq(bitround<uint16_t>(2), 2);
	assert_eq(bitround<uint16_t>(3), 4);
	assert_eq(bitround<uint16_t>(4), 4);
	assert_eq(bitround<uint32_t>(0), 1);
	assert_eq(bitround<uint32_t>(1), 1);
	assert_eq(bitround<uint32_t>(2), 2);
	assert_eq(bitround<uint32_t>(3), 4);
	assert_eq(bitround<uint32_t>(4), 4);
	assert_eq(bitround<uint64_t>(0), 1);
	assert_eq(bitround<uint64_t>(1), 1);
	assert_eq(bitround<uint64_t>(2), 2);
	assert_eq(bitround<uint64_t>(3), 4);
	assert_eq(bitround<uint64_t>(4), 4);
}
test("ilog2", "", "")
{
	//assert_eq(ilog2(0), -1);
	assert_eq(ilog2(1), 0);
	assert_eq(ilog2(2), 1);
	assert_eq(ilog2(3), 1);
	assert_eq(ilog2(4), 2);
	assert_eq(ilog2(7), 2);
	assert_eq(ilog2(8), 3);
	assert_eq(ilog2(15), 3);
	assert_eq(ilog2(16), 4);
	assert_eq(ilog2(31), 4);
	assert_eq(ilog2(32), 5);
	assert_eq(ilog2(63), 5);
	assert_eq(ilog2(64), 6);
	assert_eq(ilog2(127), 6);
	assert_eq(ilog2(128), 7);
	assert_eq(ilog2(255), 7);
}
test("ilog10", "", "")
{
	auto test1 = [](uint64_t n)
	{
		testctx(tostring(n))
		{
			if (n)
				assert_eq(ilog10(n), snprintf(NULL,0, "%" PRIu64, n)-1);
			if ((uint32_t)n)
				assert_eq(ilog10((uint32_t)n), snprintf(NULL,0, "%" PRIu32, (uint32_t)n)-1);
		}
	};
	for (uint64_t i=1;i!=0;i*=2)
	{
		test1(i-1);
		test1(i);
	}
	for (uint64_t i=1;i!=7766279631452241920;i*=10) // 7766279631452241920 = (uint64_t)100000000000000000000
	{
		test1(i-1);
		test1(i);
	}
}
test("modular_inv", "", "")
{
	assert_eq(modular_inv(0x12345)*0x12345, 1);
	assert_eq(modular_inv(123, 1234567)*123%1234567, 1);
}

test("test_nomalloc", "", "")
{
	test_skip("unfixable expected-failure");
	if (RUNNING_ON_VALGRIND) test_expfail("Valgrind doesn't catch new within test_nomalloc, rerun with gdb or standalone");
}

static int g_x;
static int y()
{
	g_x = 0;
	contextmanager(g_x=1, g_x=2)
	{
		assert_eq(g_x, 1);
		return 42;
	}
	assert_unreachable(); // both gcc and clang think this is reachable and throw warnings
	return -1;
}
test("contextmanager", "", "")
{
	g_x = 0;
	contextmanager(g_x=1, g_x=2)
	{
		assert_eq(g_x, 1);
	}
	assert_eq(g_x, 2);
	g_x = 0;
	assert_eq(y(), 42);
	assert_eq(g_x, 2);
}
test("endian", "", "")
{
	union { uint8_t a[2]; uint16_t b; } c;
	c.b = 0x0100;
	assert_eq(c.a[0], END_BIG);
}

test("array_size", "", "")
{
	int a[5];
	static_assert(ARRAY_SIZE(a) == 5);
	//int b[0];
	//static_assert(ARRAY_SIZE(b) == 0);
}

test("tuple", "", "")
{
	tuple<int,short,short,int> a;
	a = { 1, 2, 3, 4 };
	tuple<int,short,short,int> b = a;
	b = a;
	tuple<int,short,short,int> c(1, 2, 3, 4);
	tuple<int,short,short,int> d { 1, 2, 3, 4 };
	
	auto[e,f,g,h] = a;
	
	assert_eq(f, 2);
	static_assert(sizeof(a) == sizeof(int)*2+sizeof(short)+2);
}

#ifdef __unix__
#include <sys/mman.h>
#include <unistd.h>
#endif

test("load_small", "", "")
{
#ifdef __unix__
	size_t pagesize = sysconf(_SC_PAGESIZE);
	uint8_t * pages = (uint8_t*)mmap(nullptr, pagesize*3, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	mprotect(pages, pagesize, PROT_NONE);
	mprotect(pages+pagesize*2, pagesize, PROT_NONE);
#else
	size_t pagesize = 64; // just pick something random
	uint8_t * pages = xmalloc(pagesize*3);
#endif
	
	memset(pages+pagesize, 0xA5, pagesize);
	
	auto test2 = [&](uint8_t* ptr, const char * bytes, size_t len) {
		memcpy(ptr, bytes, len);
		{
			uint32_t expect;
			memcpy(&expect, bytes, sizeof(uint32_t));
			if (END_BIG)
				expect >>= (sizeof(uint32_t)-len)*8;
			assert_eq(load_small<uint32_t>(ptr, len), expect);
		}
#ifdef __SSE2__
		{
			__m128i actual = _mm_loadu_si128_highundef((__m128i*)ptr, len);
			assert(!memcmp(&actual, bytes, len));
			
			__m128i expect = _mm_setzero_si128();
			memcpy(&expect, bytes, sizeof(uint32_t));
			actual = _mm_loadu_si128_highzero((__m128i*)ptr, len);
			assert(!memcmp(&actual, &expect, 16));
			
			// used to verify the condition in _mm_loadu_si128_highundef
			/*
			int n=0;
			for (int src=0;src<64;src++)
			for (int len=1;len<=16;len++)
			{
				bool safe1 = LIKELY((63&(uintptr_t)src) <= 48) || ((48-(uintptr_t)src)&63) < 48+(uintptr_t)len;
				bool safe2 = LIKELY((63&(uintptr_t)src) <= 48) || ((-src)&15) < len;
				printf("%c", "uUSs"[safe1+safe1+safe2]);
				if (len==16) printf(" src=%d\n",src);
				if (safe1 != safe2) n++;
			}
			printf("%d failures\n",n);
			*/
		}
#endif
	};
	auto test1 = [&](const char * bytes, size_t len) {
		test2(pages+pagesize, bytes, len);
		test2(pages+pagesize*2-len, bytes, len);
	};
	
	test1("\x11\x00\x00\x00", 1);
	test1("\x11\x22\x00\x00", 2);
	test1("\x11\x22\x33\x00", 3);
	test1("\xC1\x22\x33\x89", 4);
	test1("\x40\x22\x33\x08", 4);
	
#ifdef __unix__
	munmap(pages, pagesize*3);
#else
	free(pages);
#endif
}

test("hash", "", "")
{
	uint8_t buf[64];
	for (size_t len=0;len<=64;len++)
	{
		if (len != 0)
			buf[len-1] = 0;
		set<size_t> hashes;
		size_t max_byte = (len <= sizeof(size_t) ? 255 : 1);
		for (size_t by=0;by<len;by++)
		{
			for (size_t val=1;val<=max_byte;val++)
			{
				buf[by] = val;
				hashes.add(hash(bytesr(buf, len)));
			}
			buf[by] = 0;
		}
		hashes.add(hash(bytesr(buf, len)));
		testctx(tostring(len))
			assert_eq(hashes.size(), len*max_byte+1);
	}
}

#if 0
static void bench(const uint8_t * buf, int len)
{
	benchmark b;
	while (b)
	{
		b.launder(hash(bytesr(buf, b.launder(len))));
	}
	printf("size %d - %f/s - %fGB/s\n", len, b.per_second(), b.per_second()*len/1024/1024/1024);
}

test("hash bench", "", "")
{
	assert(!RUNNING_ON_VALGRIND);
	
	uint8_t buf[65536];
	uint32_t k = ~0;
	for (size_t i : range(65536))
	{
		k = (-(k&1) & 0xEDB88320) ^ (k>>1);
		buf[i] = k;
	}
	
	bench(buf, 65536);
	bench(buf, 1024);
	bench(buf, 256);
	bench(buf, 64);
	bench(buf, 32);
	bench(buf, 31);
	bench(buf, 24);
	bench(buf, 16);
	bench(buf, 12);
	bench(buf, 8);
	bench(buf, 7);
	bench(buf, 6);
	bench(buf, 5);
	bench(buf, 4);
	bench(buf, 3);
	bench(buf, 2);
	bench(buf, 1);
	bench(buf, 0);
}
#endif

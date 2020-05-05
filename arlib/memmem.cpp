#include "global.h"

#if defined(_WIN32) || defined(__x86_64__) || defined(ARLIB_TEST)
// Unlike musl and glibc, this program uses a rolling hash, not the twoway algorithm.
// Twoway is often faster, but average is roughly the same, and rolling has more stable performance.
// (Unless you repeatedly get hash collisions, but that requires a repetitive haystack and an adversarial needle.)

// I could write a SWAR program for small needles, but there's no need. it's fast on x86, and libc covers it on everything else.
// I'll revisit this decision if I port to Windows on ARM.

#if defined(__i386__) || defined(__x86_64__)
#include <immintrin.h>
#undef memmem

// Loads len (1 to 16) bytes from src to a __m128i. src doesn't need any particular alignment. The result's high bytes are undefined.
static inline __m128i load_sse2_small(const uint8_t * src, size_t len) __attribute__((target("sse2")));
static inline __m128i load_sse2_small(const uint8_t * src, size_t len)
{
	// rule 1: do not touch a 4096-aligned page that the safe version does not
	// rule 2: do not permit the compiler to prove any part of this program is UB
	// rule 3: as fast as possible
	
	// doing two tests is usually slower, but the first half is true 99.6% of the time,
	//  and simplifying the common case outweighs making the rare case more expensive
	// combined test: ((4080-src)&4095) < 4080+len
	// the obvious one: !(((src+15)^(src+len-1)) & 4096)
	if (LIKELY(((uintptr_t)src&4080) != 4080) || ((-(uintptr_t)src)&15) < len)
	{
		__asm__("" : "+r"(src)); // make sure compiler can't prove anything about the below load
		return _mm_loadu_si128((__m128i*)src);
	}
	else
	{
		// if an extended read would inappropriately hit the next page, copy it to the stack, then do an unaligned read
		// going via memory is ugly, but there's no better instruction
		// machine-wise, it'd be safe to shrink tmp to 16 bytes, putting return address or whatever in the high bytes,
		//  but that saves nothing, and gcc could optimize the UB
		uint8_t tmp[32] __attribute__((aligned(16)));
		__asm__("" : "+r"(src), "=m"(tmp)); // reading uninitialized variables is UB, confuse gcc some more
		_mm_store_si128((__m128i*)tmp, _mm_load_si128((__m128i*)(~15&(uintptr_t)src)));
		return _mm_loadu_si128((__m128i*)(tmp+(15&(uintptr_t)src)));
	}
}

// Works on needles of length 16 or less, but it gets slower for big ones.
static const uint8_t * memmem_sse42(const uint8_t *, size_t, const uint8_t *, size_t) __attribute__((target("sse4.2")));
static const uint8_t * memmem_sse42(const uint8_t * haystack, size_t haystacklen, const uint8_t * needle, size_t needlelen)
{
	__m128i needle_sse = load_sse2_small(needle, needlelen);
	
	size_t step = 16+1-needlelen;
	
#define CMPSTR_FLAGS (_SIDD_UBYTE_OPS|_SIDD_CMP_EQUAL_ORDERED|_SIDD_LEAST_SIGNIFICANT)
	while (haystacklen >= 16)
	{
		__m128i haystack_sse = _mm_loadu_si128((__m128i*)haystack);
		int pos = _mm_cmpestri(needle_sse, needlelen, haystack_sse, 16, CMPSTR_FLAGS);
		
		if (pos < (int)step)
			return haystack + pos;
		
		haystack += step; // oddly enough, using 'pos' instead reduces performance. I guess cmpestri latency is high.
		haystacklen -= step; // do not change without adding a haystacklen!=0 check below
	}
	
	// haystacklen is known >= 1
	// the only way for it to be 0 is if step=16, which means needlelen=1, but that takes the memchr path
	__m128i haystack_sse = load_sse2_small(haystack, haystacklen);
	int pos = _mm_cmpestri(needle_sse, needlelen, haystack_sse, haystacklen, CMPSTR_FLAGS);
	if (pos < (int)(haystacklen+1-needlelen))
		return haystack + pos;
#undef CMPSTR_FLAGS
	
	return NULL;
}
#endif


static const uint8_t * memmem_rollhash(const uint8_t * haystack, size_t haystacklen, const uint8_t * needle, size_t needlelen)
{
	size_t hash_in = 131; // lowest prime >= 128 - fairly arbitrarily chosen
	size_t needle_hash = 0;
	
	uint32_t bytes_used[256/32] = {};
	for (size_t n : range(needlelen))
	{
		uint8_t ch = needle[n];
		bytes_used[ch/32] |= 1u<<(ch&31);
		
		needle_hash = needle_hash*hash_in + ch;
	}
	
	const uint8_t * haystackend = haystack+haystacklen;
	
again:
	if (haystack+needlelen > haystackend)
		return NULL;
	
	size_t haystack_hash = 0;
	size_t hash_out = 1;
	for (size_t n = needlelen; n--; )
	{
		uint8_t ch = haystack[n];
		if (!(bytes_used[ch/32] & (1u<<(ch&31))))
		{
			haystack = haystack+n+1;
			goto again;
		}
		
		haystack_hash += ch * hash_out;
		hash_out *= hash_in;
	}
	
	size_t pos = 0;
	while (true)
	{
		if (needle_hash == haystack_hash && !memcmp(haystack+pos, needle, needlelen))
			return haystack + pos;
		
		if (haystack+pos >= haystackend)
			return NULL;
		
		haystack_hash *= hash_in;
		haystack_hash -= haystack[pos]*hash_out;
		haystack_hash += haystack[pos+needlelen];
		
		uint8_t ch = haystack[pos+needlelen];
		if (!(bytes_used[ch/32] & (1u<<(ch&31))))
		{
			haystack = haystack+pos+needlelen;
			goto again;
		}
		
		pos++;
	}
}

void* memmem_arlib(const void * haystack, size_t haystacklen, const void * needle, size_t needlelen) __attribute__((pure));
void* memmem_arlib(const void * haystack, size_t haystacklen, const void * needle, size_t needlelen)
{
	if (UNLIKELY(needlelen == 0)) return (void*)haystack;
	
	const void * hay_orig = haystack;
	haystack = memchr(haystack, *(uint8_t*)needle, haystacklen);
	if (!haystack) return NULL;
	if (needlelen == 1) return (void*)haystack;
	
	haystacklen -= (uint8_t*)haystack - (uint8_t*)hay_orig;
	if (UNLIKELY(needlelen > haystacklen)) return NULL;
	
#if defined(__i386__) || defined(__x86_64__)
	if (needlelen <= 15
#ifndef __SSE4_2__
		&& __builtin_cpu_supports("sse4.2") // this should be optimized if -msse4.2, but isn't, so more ifdef
#endif
	)
		return (void*)memmem_sse42((uint8_t*)haystack, haystacklen, (uint8_t*)needle, needlelen);
#endif
	
#if !defined(_WIN32) && !defined(ARLIB_TEST) // for long needles, use libc; we're roughly equally fast, and code reuse means smaller
	return memmem(haystack, haystacklen, needle, needlelen);
#endif
	
	return (void*)memmem_rollhash((uint8_t*)haystack, haystacklen, (uint8_t*)needle, needlelen);
}



#ifdef ARLIB_TEST
#include "test.h"
#include "os.h"

typedef void*(*memmem_t)(const void * haystack, size_t haystacklen, const void * needle, size_t needlelen);

static void unpack(bytearray& out, const char * rule, size_t* star)
{
	out.reset();
	
	size_t start = 0;
	for (size_t n=0;rule[n];n++)
	{
		if (rule[n] == '(' || rule[n] == '*')
		{
			size_t outstart = out.size();
			out.resize(outstart + n-start);
			memcpy(out.ptr()+outstart, rule+start, n-start);
			start = n+1;
			if (rule[n] == '*' && star)
			{
				*star = out.size();
				star = NULL;
			}
		}
		if (rule[n] == ')')
		{
			size_t outstart = out.size();
			char* tmp;
			size_t repeats = strtol(rule+n+1, &tmp, 10);
			
			out.resize(outstart + (n-start)*repeats);
			for (size_t m : range(repeats))
				memcpy(out.ptr()+outstart+(n-start)*m, rule+start, n-start);
			
			n = tmp-rule-1;
			start = n+1;
		}
	}
	
	size_t outstart = out.size();
	out.resize(outstart + strlen(rule)-start);
	memcpy(out.ptr()+outstart, rule+start, strlen(rule)-start);
}
static uint64_t test1_raw(memmem_t memmem, void * haystack, size_t haystacklen, void * needle, size_t needlelen, void* expected)
{
	benchmark b(100000);
	while (b)
	{
		void* ret = memmem(haystack, haystacklen, needle, needlelen);
		assert_eq(ret, expected);
	}
	return b.per_second();
}
static void test1(memmem_t memmem, const char * haystack, const char * needle)
{
	size_t match_pos = -1;
	bytearray full_hay;
	unpack(full_hay, haystack, &match_pos);
	bytearray full_needle;
	unpack(full_needle, needle, NULL);
	
	void* expected = (match_pos == (size_t)-1 ? NULL : full_hay.ptr()+match_pos);
	
#ifndef _WIN32
	uint64_t perf_libc = test1_raw(memmem, full_hay.ptr(), full_hay.size(), full_needle.ptr(), full_needle.size(), expected);
#else
	uint64_t perf_libc = 0;
#endif
	uint64_t perf_arlib = test1_raw(memmem_arlib, full_hay.ptr(), full_hay.size(), full_needle.ptr(), full_needle.size(), expected);
	
	const char * winner = " | ";
	if ((double)perf_libc / perf_arlib > 1.5) winner = "*| ";
	if ((double)perf_arlib / perf_libc > 1.5) winner = " |*";
	printf("%8lu%s%8lu | %s / %s\n", perf_libc, winner, perf_arlib, haystack, needle);
}

test("memmem", "", "string")
{
	test_skip_force("way too slow, also prints a bunch of stuff");
	
	puts("\nlibc     | Arlib");
	
	testcall(test1(memmem, "*aaaaaaaa", "aaaaaaa"));
	
	testcall(test1(memmem, "(b)100000", "a"));
	testcall(test1(memmem, "(b)100000*a", "a"));
	testcall(test1(memmem, "(b)100000b*a", "a"));
	testcall(test1(memmem, "(b)100000bb*a", "a"));
	testcall(test1(memmem, "(b)100000bbb*a", "a"));
	testcall(test1(memmem, "(b)100000bbbb*a", "a"));
	testcall(test1(memmem, "(b)100000*abbbb", "a"));
	testcall(test1(memmem, "(b)100000b*abbb", "a"));
	testcall(test1(memmem, "(b)100000bb*abb", "a"));
	testcall(test1(memmem, "(b)100000bbb*ab", "a"));
	testcall(test1(memmem, "(b)100000bbbb*a", "a"));
	
	testcall(test1(memmem, "(ab)100000a", "aa"));
	testcall(test1(memmem, "(ab)100000*aa", "aa"));
	testcall(test1(memmem, "(ab)100000b*aa", "aa"));
	testcall(test1(memmem, "(ab)100000*aab", "aa"));
	testcall(test1(memmem, "(ab)100000*aa(bb)31", "aa"));
	testcall(test1(memmem, "(ab)100000*(aa)16", "aa"));
	testcall(test1(memmem, "babababababababababababababab*aa", "aa"));
	testcall(test1(memmem, "ababababababab*aa", "aa"));
	
	testcall(test1(memmem, "a(bb)100000", "aa"));
	testcall(test1(memmem, "a(bb)100000*aa", "aa"));
	testcall(test1(memmem, "a(bb)100000b*aa", "aa"));
	testcall(test1(memmem, "a(bb)100000*aab", "aa"));
	testcall(test1(memmem, "a(bb)100000*aa(bb)31", "aa"));
	testcall(test1(memmem, "a(bb)100000*(aa)16", "aa"));
	testcall(test1(memmem, "babababababababababababababab*aa", "aa"));
	testcall(test1(memmem, "ababababababab*aa", "aa"));
	testcall(test1(memmem, "(bb)100000a", "aa"));
	testcall(test1(memmem, "(bb)100000abbbbbbbbb", "aa"));
	
	testcall(test1(memmem, "(ab)100000", "ba(ab)32"));
	testcall(test1(memmem, "(ab)100000", "(ab)16ba(ab)16"));
	testcall(test1(memmem, "(ab)100000", "(ab)32ba"));
	testcall(test1(memmem, "b(a)100000", "(b)1000"));
	
	testcall(test1(memmem, "(ab)1000000", "(ab)10000aa(ab)10000"));
	testcall(test1(memmem, "(ab)10000ab(ab)10000", "(ab)10000aa(ab)10000"));
	testcall(test1(memmem, "*(ab)10000aa(ab)10000", "(ab)10000aa(ab)10000"));
	testcall(test1(memmem, "*(a)256", "(a)16"));
	
	testcall(test1(memmem, "(cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
	                        "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
	                        "abababababababababababababababababababababababababababababababab"
	                        "abababababababababababababababababababababababababababababababab)10000", "ababababbbabababa"));
}

test("load_sse2_small","","")
{
	uint8_t* page;
	if (posix_memalign((void**)&page, 4096, 8192) != 0) abort();
	autofree<uint8_t> holder = page;
	
	for (int i=0;i<8192;i++)
		page[i] = i&31;
	
	auto test1 = [&](int start, int size) {
		assert_gte(start, size);
		uint8_t tmp[16];
		_mm_storeu_si128((__m128i*)tmp, load_sse2_small(page+8192-start, size));
		
		bytesr expected(page+8192-start, size);
		bytesr actual(tmp, size);
		assert_eq(actual, expected);
	};
	
	test1(13, 13);
	test1(16, 16);
	test1(16, 8);
	test1(16, 11);
	test1(27, 16);
	test1(4100, 16);
	
	// used to verify the condition in load_sse2_small
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
#endif

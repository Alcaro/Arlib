#include "stringconv.h"
#include <stdio.h>
#include <math.h>
#include <float.h>
#include "test.h"

#define FROMFUNC(t,frt,f) \
	bool fromstring(cstring s, t& out) \
	{ \
		out = 0; \
		char * tmp; /* odd that this one isn't overloaded, like strchr */ \
		frt ret = f(s, &tmp, 10); \
		if (*tmp || (t)ret != (frt)ret) return false; \
		out = ret; \
		return true; \
	}

//specification: if the input is a hex number, return something strtoul accepts
//otherwise, return something that strtoul rejects
//this means drop the 0x
static const char * drop0x(const char * in)
{
	if (in[0]=='0' && in[1]!='0') return in+1;
	else return in;
}

#define FROMFUNCHEX(t,frt,f) \
	FROMFUNC(t,frt,f) \
	\
	bool fromstringhex(cstring s, t& out) \
	{ \
		const char * in = drop0x(s); \
		out = 0; \
		char * tmp; /* odd that this one isn't overloaded, like strchr */ \
		frt ret = f(in, &tmp, 16); \
		if (*tmp || (t)ret != (frt)ret) return false; \
		out = ret; \
		return true; \
	}

FROMFUNC(   signed char,    long,          strtol)
FROMFUNCHEX(unsigned char,  unsigned long, strtoul)
FROMFUNC(   signed short,   long,          strtol)
FROMFUNCHEX(unsigned short, unsigned long, strtoul)
FROMFUNC(   signed int,     long,          strtol)
FROMFUNCHEX(unsigned int,   unsigned long, strtoul)
FROMFUNC(   signed long,    long,          strtol)
FROMFUNCHEX(unsigned long,  unsigned long, strtoul)
FROMFUNC(   signed long long,   long long,          strtoll)
FROMFUNCHEX(unsigned long long, unsigned long long, strtoull)

bool fromstring(cstring s, double& out)
{
	out = 0;
	char * tmp;
	double ret = strtod(s, &tmp);
	if (*tmp || ret==HUGE_VAL || ret==-HUGE_VAL) return false;
	out = ret;
	return true;
}

//strtof exists in C99, but let's not use that
bool fromstring(cstring s, float& out)
{
	out = 0;
	double tmp;
	if (!fromstring(s, tmp)) return false;
	if (tmp < -FLT_MAX || tmp > FLT_MAX) return false;
	out = tmp;
	return true;
}

bool fromstring(cstring s, bool& out)
{
	if (s=="false" || s=="0")
	{
		out=false;
		return true;
	}
	
	if (s=="true" || s=="1")
	{
		out=true;
		return true;
	}
	
	out=false;
	return false;
}


string tostringhex(arrayview<byte> val)
{
	string ret;
	arrayvieww<byte> retb = ret.construct(val.size()*2);
	for (size_t i=0;i<val.size();i++)
	{
		sprintf((char*)retb.slice(i*2, 2).ptr(), "%.2X", val[i]);
	}
	return ret;
}

bool fromstringhex(cstring s, arrayvieww<byte> val)
{
	if (val.size()*2 != s.length()) return false;
	bool ok = true;
	for (size_t i=0;i<val.size();i++)
	{
		ok &= fromstringhex(s.csubstr(i*2, i*2+2), val[i]);
	}
	return ok;
}
bool fromstringhex(cstring s, array<byte>& val)
{
	val.resize(s.length()/2);
	return fromstringhex(s, (arrayvieww<byte>)val);
}


template<typename T> void testunhex(const char * S, unsigned long long V)
{
	T a;
	assert_eq(fromstringhex(S, a), true);
	assert_eq(a, V);
}
test()
{
	testcall(testunhex<unsigned char     >("aa", 0xaa));
	testcall(testunhex<unsigned char     >("AA", 0xAA));
	testcall(testunhex<unsigned short    >("aaaa", 0xaaaa));
	testcall(testunhex<unsigned short    >("AAAA", 0xAAAA));
	testcall(testunhex<unsigned int      >("aaaaaaaa", 0xaaaaaaaa));
	testcall(testunhex<unsigned int      >("AAAAAAAA", 0xAAAAAAAA));
	testcall(testunhex<unsigned long     >("aaaaaaaa", 0xaaaaaaaa)); // this is sometimes 64bit, but good enough
	testcall(testunhex<unsigned long     >("AAAAAAAA", 0xAAAAAAAA));
	testcall(testunhex<unsigned long long>("aaaaaaaaaaaaaaaa", 0xaaaaaaaaaaaaaaaa));
	testcall(testunhex<unsigned long long>("AAAAAAAAAAAAAAAA", 0xAAAAAAAAAAAAAAAA));
	
	byte foo[4] = {0x12,0x34,0x56,0x78};
	assert_eq(tostringhex(arrayview<byte>(foo)), "12345678");
	
	assert(fromstringhex("87654321", arrayvieww<byte>(foo)));
	assert_eq(foo[0], 0x87); assert_eq(foo[1], 0x65); assert_eq(foo[2], 0x43); assert_eq(foo[3], 0x21);
	
	array<byte> bar;
	assert(fromstringhex("1234567890", bar));
	assert_eq(bar.size(), 5);
	assert_eq(bar[0], 0x12);
	assert_eq(bar[1], 0x34);
	assert_eq(bar[2], 0x56);
	assert_eq(bar[3], 0x78);
	assert_eq(bar[4], 0x90);
	
	assert(!fromstringhex("123456", arrayvieww<byte>(foo))); // not 4 bytes
	assert(!fromstringhex("1234567", bar)); // odd length
	assert(!fromstringhex("0x123456", bar)); // invalid symbol
}

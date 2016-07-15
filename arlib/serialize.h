#pragma once
#include "global.h"

//public API:
//for serializable classes:

//must be last
#define SER(member) +_s.execute_base(#member, member)

//these are optional and go in front of SER, in any order
//it would be better to do this via reflection and attributes, but it doesn't seem like C++17 will have that, and Arlib is C++11 anyways.
#define SER_HEX SER_OPT(hex)

#define onserialize() \
	template<typename _T> \
	void serialize(_T& _s)

//example:
//onserialize() { SER_HEX SER(x); }


//for serializers:
//inherit from serializer_base<your class>
//implement template<typename T> void serialize(const char * name, T& member, const serialize_opts& opts)
//both public


//serializer options:
#define SER_OPTS(na, a) /* na = no argument to SER_name, a = has argument*/ \
	na(bool, hex) \

//then poke opts.hex

//for an example of everything, see serialize.cpp



//implementation follows (serialize.cpp is just a test). warning: ugly as fuck

struct serialize_opts {
#define X(t, n) t n;
	SER_OPTS(X, X)
#undef X
	serialize_opts()
	{
#define X(t, n) n=t();
	SER_OPTS(X, X)
#undef X
	}
	
#define Xn(t, n) static serialize_opts opt_##n() { serialize_opts ret; ret.n=1; return ret; }
#define Xa(t, n) static serialize_opts opt_##n(t v) { serialize_opts ret; ret.n=v; return ret; }
	SER_OPTS(Xn, Xa)
#undef Xn
#undef Xa
	
	serialize_opts operator+() { return *this; }
	serialize_opts operator+(const serialize_opts& right)
	{
#define X(t, n) n |= right.n;
		SER_OPTS(X, X)
#undef X
		return *this;
	}
};

template<typename Tser, typename Tmem>
struct serialize_execute {
	Tser* parent;
	const char * name;
	Tmem& member;
	serialize_execute(Tser* parent, const char * name, Tmem& member) : parent(parent), name(name), member(member) {}
};

template<typename Tser, typename Tmem>
void operator+(const serialize_execute<Tser,Tmem>& exec)
{
	serialize_opts opts;
	exec.parent->serialize(exec.name, exec.member, opts);
}

template<typename Tser, typename Tmem>
void operator+(const serialize_opts& opts, const serialize_execute<Tser,Tmem>& exec)
{
	exec.parent->serialize(exec.name, exec.member, opts);
}

template<typename real>
struct serializer_base {
	template<typename T>
	serialize_execute<real,T> execute_base(const char * name, T& val)
	{
		return serialize_execute<real,T>((real*)this, name, val);
	}
};

#define SER_OPT(name, ...) +serialize_opts::opt_##name(__VA_ARGS__)

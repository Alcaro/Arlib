#include "serialize.h"
#include "test.h"
#include "bml.h"

/*

{ a; b; }
a=1
b=2

{ a={ b; c; } d; }
a
  b=1
  c=2
d=3

*/

#define SERIALIZE_CORE(member) s(STR(member), member);
#define SERIALIZE(...) template<typename T> void serialize(T& s) { PPFOREACH(SERIALIZE_CORE, __VA_ARGS__); }

class bmlserialize_impl {
	bmlwriter w;
	template<typename T> friend string bmlserialize(T& item);
	
public:
	
	static const bool serializing = true;
	
	template<typename T> void operator()(cstring name, T& item)
	{
		w.enter(name, "");
		item.serialize(*this);
		w.exit();
	}
	
#define LEAF(T) void operator()(cstring name, T& item) { w.node(name, tostring(item)); }
	LEAF(char);
	LEAF(int);
	LEAF(unsigned int);
	LEAF(bool);
	LEAF(float);
	LEAF(time_t);
#undef LEAF
};

template<typename T> string bmlserialize(T& item)
{
	bmlserialize_impl s;
	item.serialize(s);
	return s.w.finish();
}



class bmlunserialize_impl {
	bmlparser p;
	
	cstring thisnode;
	cstring thisval;
	
	void exit()
	{
		int depth = 1;
		while (depth > 0)
		{
			switch (p.next().action)
			{
				case bmlparser::enter: depth++; break;
				case bmlparser::exit: depth--; break;
				case bmlparser::finish: return;
			}
		}
	}
	
	bmlunserialize_impl(cstring bml) : p(bml) {}
	template<typename T> friend T bmlunserialize(cstring bml);
	
	void next() {}
	
	template<typename T> void item(T& out)
	{
		while (true)
		{
			bmlparser::event ev = p.next();
			if (ev.action == bmlparser::enter)
			{
				thisnode = ev.name;
				thisval = ev.value;
				out.serialize(*this);
				exit();
			}
			if (ev.action == bmlparser::exit || ev.action == bmlparser::finish) break;
		}
	}
	
#define LEAF(T) void item(T& out) { out = fromstring<T>(thisval); }
	LEAF(char);
	LEAF(int);
	LEAF(unsigned int);
	LEAF(bool);
	LEAF(float);
	LEAF(time_t);
#undef LEAF
	
public:
	
	static const bool serializing = false;
	
	template<typename T> void operator()(cstring name, T& out)
	{
		puts(thisnode+"=="+name);
		if (thisnode==name)
		{
			item(out);
			next();
		}
	}
};

template<typename T> T bmlunserialize(cstring bml)
{
	T out{};
	bmlunserialize_impl s(bml);
	s.item(out);
	return out;
}



#ifdef ARLIB_TEST
struct ser1 {
	int a;
	int b;
	
	SERIALIZE(a, b);
};

struct ser2 {
	ser1 c;
	ser1 d;
	
	SERIALIZE(c, d);
};

struct ser3 {
	int a;
	int b;
	int c;
	int d;
	
	SERIALIZE(a, b, c, d);
};

struct ser4 {
	ser3 mem;
	int count = 0;
	template<typename T> void serialize(T& s) { mem.serialize(s); count++; }
};

test()
{
	{
		ser1 item;
		item.a = 1;
		item.b = 2;
		
		assert_eq(bmlserialize(item), "a=1\nb=2");
	}
	
	{
		ser2 item;
		item.c.a = 1;
		item.c.b = 2;
		item.d.a = 3;
		item.d.b = 4;
		assert_eq(bmlserialize(item), "c a=1 b=2\nd a=3 b=4");
	}
}

test()
{
	{
		ser1 item = bmlunserialize<ser1>("a=1\nb=2");
		assert_eq(item.a, 1);
		assert_eq(item.b, 2);
	}
	
	{
		ser2 item = bmlunserialize<ser2>("c a=1 b=2\nd a=3 b=4");
		assert_eq(item.c.a, 1);
		assert_eq(item.c.b, 2);
		assert_eq(item.d.a, 3);
		assert_eq(item.d.b, 4);
	}
	
	{
		ser2 item = bmlunserialize<ser2>("d b=4 a=3\nc a=1 b=2");
		assert_eq(item.c.a, 1);
		assert_eq(item.c.b, 2);
		assert_eq(item.d.a, 3);
		assert_eq(item.d.b, 4);
	}
	
	{
		ser1 item = bmlunserialize<ser1>("a=1\nb=2\na=3\na=4");
		assert_eq(item.a, 4);
		assert_eq(item.b, 2);
	}
	
	{
		ser4 item = bmlunserialize<ser4>("a=1\nb=2\nc=3\nd=4");
		assert_eq(item.count, 1); // must call serialize() only once
	}
}
#endif

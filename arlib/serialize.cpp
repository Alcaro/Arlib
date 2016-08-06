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

#define SERIALIZER template<typename T> void serialize(T& serializer)
#define SERIALIZE(x) serializer(STR(x), x)

class bmlserializer {
	bmlwriter w;
	
public:
	
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
	
	//this lacks the leaf-node overloads, use only for structs
	//but only bmlserialize() should use this anyways.
	template<typename T> static string root(T& item)
	{
		bmlserializer s;
		item.serialize(s);
		return s.w.finish();
	}
};

template<typename T> string bmlserialize(T& item)
{
	return bmlserializer::root(item);
}

#ifdef ARLIB_TEST
struct ser1 {
	int a;
	int b;
	
	SERIALIZER { SERIALIZE(a); SERIALIZE(b); }
};

struct ser2 {
	ser1 c;
	ser1 d;
	
	SERIALIZER { SERIALIZE(c); SERIALIZE(d); }
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
	
	return true;
}
#endif

#include "serialize.h"
#include "test.h"
#include <string.h>

#ifdef ARLIB_TEST
struct sertest {
int a;
int b;

sertest()
{
	a=16;
	b=32;
}

onserialize() {
	SER(a);
	SER_HEX SER(b);
}
};

class serializer_test : public serializer_base<serializer_test> {
public:
	int phase;
	
	template<typename T>
	void serialize(const char * name, T& member, const serialize_opts& opts)
	{
		if(0);
		else if (phase==0 && !strcmp(name, "a") && member==16 && opts.hex==false) phase++;
		else if (phase==1 && !strcmp(name, "b") && member==32 && opts.hex==true) phase++;
		else phase=-1;
		member++;
	}
};

test()
{
	serializer_test test;
	test.phase=0;
	sertest item;
	item.serialize(test);
	
	return test.phase==2 && item.a==17 && item.b==33;
}
#endif

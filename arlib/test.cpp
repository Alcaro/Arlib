#ifdef ARLIB_TEST
#include "test.h"

struct testlist {
	bool(*func)();
	const char * name;
	testlist* next;
};

static testlist* g_testlist;

_testdecl::_testdecl(bool(*func)(), const char * name)
{
	testlist* next = malloc(sizeof(testlist));
	next->func = func;
	next->name = name;
	next->next = g_testlist;
	g_testlist = next;
}

#undef main
int main(int argc, char* argv[])
{
	int count[2]={0,0};
	testlist* test = g_testlist;
	while (test)
	{
		testlist* next = test->next;
		printf("Testing %s...", test->name);
		bool pass = test->func();
		printf("%i", pass);
		count[pass]++;
		puts(pass ? " pass" : " fail");
		free(test);
		test = next;
	}
	printf("Passed %i, failed %i\n", count[1], count[0]);
	return 0;
}

test()
{
	return true;
}
#endif

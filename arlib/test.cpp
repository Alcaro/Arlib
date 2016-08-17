#ifdef ARLIB_TEST
#include "test.h"

struct testlist {
	void(*func)();
	const char * name;
	testlist* next;
};

static testlist* g_testlist;

_testdecl::_testdecl(void(*func)(), const char * name)
{
	testlist* next = malloc(sizeof(testlist));
	next->func = func;
	next->name = name;
	next->next = g_testlist;
	g_testlist = next;
}

static bool thisfail;

void _testfail(cstring why)
{
	if (!thisfail) puts(why); // discard multiple failures from same test, they're probably caused by same thing
	thisfail = true;
}

void _testeqfail(cstring name, cstring expected, cstring actual)
{
	if (expected.contains("\n") || actual.contains("\n"))
	{
		_testfail("\nFailed assertion "+name+"\nexpected:\n"+expected+"\nactual:\n"+actual);
	}
	else
	{
		_testfail("\nFailed assertion "+name+": expected "+expected+", got "+actual);
	}
}

#undef main // the real main is #define'd to something stupid on test runs
int main(int argc, char* argv[])
{
	int count[2]={0,0};
	
	//flip list backwards
	//order of static initializers is implementation defined, but this makes output better under gcc
	testlist* test = g_testlist;
	g_testlist = NULL;
	while (test)
	{
		testlist* next = test->next;
		test->next = g_testlist;
		g_testlist = test;
		test = next;
	}
	
	test = g_testlist;
	while (test)
	{
		testlist* next = test->next;
		printf("Testing %s...", test->name);
		thisfail = false;
		test->func();
		count[thisfail]++;
		if (!thisfail) puts(" pass");
		free(test);
		test = next;
	}
	printf("Passed %i, failed %i\n", count[0], count[1]);
	return 0;
}

test() {}
test() {}
#endif

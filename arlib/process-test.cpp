#include "process.h"
#include "test.h"

#ifdef __linux__
test()
{
	test_skip("too slow and noisy under Valgrind");
	
	//there are a couple of race conditions here, but I believe they're all safe
	{
		process p;
		assert(p.launch("/bin/true"));
		int status;
		p.wait(&status);
		assert_eq(status, 0);
	}
	
	{
		process p;
		assert(p.launch("/bin/echo", "foo"));
		int status;
		p.wait(&status);
		assert_eq(status, 0);
		assert_eq(p.stdout(), "foo\n");
	}
	
	{
		process p;
		p.stdin("foo");
		assert(p.launch("/bin/cat"));
		p.wait();
		assert_eq(p.stdout(), "foo");
	}
	
	{
		process p;
		p.interact(true);
		assert(p.launch("/bin/cat"));
		p.stdin("foo");
		p.wait();
		assert_eq(p.stdout(), "foo");
	}
	
	{
		process p;
		p.interact(true);
		assert(p.launch("/bin/cat"));
		p.stdin("foo");
		usleep(10*1000); // RACE
		assert_eq(p.stdout(), "foo");
		assert(p.running());
		p.interact(false);
		usleep(1*1000); // RACE (this gets interrupted by SIGCHLD, but it's resumed)
		assert(!p.running());
	}
	
	{
		process p;
		assert(p.launch("/bin/echo", "foo"));
		assert_eq(p.stdout(), ""); // RACE
		assert_eq(p.stdout(true), "foo\n");
	}
}
#else
#error find windows equivalents
#endif

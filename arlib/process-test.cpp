#include "process.h"
#include "test.h"

#ifndef _WIN32
#include <unistd.h> // usleep
#define TRUE "/bin/true"
#define ECHO "/bin/echo"
#define YES "/usr/bin/yes"
#define LF "\n"
#define ECHO_END LF
#define CAT_FILE "/bin/cat"
#define CAT_STDIN "/bin/cat"
#define CAT_STDIN_END ""
#else
#undef TRUE // screw this, I don't need two trues
#define TRUE "cmd", "/c", "type NUL" // windows has no /bin/true, fake it
#define ECHO "cmd", "/c", "echo"
#define YES "cmd", "/c", "tree /f c:" // not actually infinite, but close enough
#define LF "\r\n"
#define ECHO_END LF
#define CAT_FILE "cmd", "/c", "type"
#define CAT_STDIN "find", "/v", "\"COPY_THE_INPUT_UNCHANGED\""
#define CAT_STDIN_END LF
#define usleep(n) Sleep((n)/1000)
#endif

test()
{
#ifdef __linux__
	test_skip("too noisy under Valgrind");
#endif
	test_skip("kinda slow");
	//there are a couple of race conditions here, but I believe they're all safe
	{
		process p;
		assert(p.launch(TRUE));
		int status;
		p.wait(&status);
		assert_eq(status, 0);
	}
	
	{
		process p;
		assert(p.launch(ECHO, "foo"));
		int status;
		p.wait(&status);
		assert_eq(status, 0);
		assert_eq(p.read(), "foo" ECHO_END);
	}
	
	{
		process p;
		p.write("foo");
		assert(p.launch(CAT_STDIN));
		p.wait();
		assert_eq(p.read(), "foo" CAT_STDIN_END);
	}
	
	{
		process p;
		p.interact(true);
		assert(p.launch(CAT_STDIN));
		p.write("foo");
		p.wait();
		assert_eq(p.read(), "foo" CAT_STDIN_END);
	}
	
	{
		process p;
		p.interact(true);
		assert(p.launch(CAT_STDIN));
		p.write("foo" LF);
		assert_eq(p.read(true), "foo" LF);
		p.write("foo" LF);
		assert_eq(p.read(true), "foo" LF);
		p.write("foo" LF);
		assert_eq(p.read(true), "foo" LF);
		p.write("foo" LF);
		assert_eq(p.read(true), "foo" LF);
	}
	
	{
		process p;
		p.error();
		assert(p.launch(CAT_FILE, "nonexist.ent"));
		p.wait();
		assert_eq(p.read(), "");
		assert(p.error() != "");
	}
	
	{
		process p;
		p.outlimit(1024);
		assert(p.launch(YES));
		p.wait();
		string out = p.read();
		assert(out.length() >= 1024); // it can read a bit more than 1K if it wants to, buffer size is 4KB
		assert(out.length() <= 8192); // on windows, limit is honored exactly
	}
	
	{
		process p;
		assert(p.launch(ECHO, "foo"));
		assert_eq(p.read(), ""); // RACE
		assert_eq(p.read(true), "foo" ECHO_END);
	}
	
	{
		string lots_of_data = "a" LF;
		while (lots_of_data.length() < 256*1024) lots_of_data += lots_of_data;
		process p;
		p.interact(true);
		assert(p.launch(CAT_STDIN));
		p.write(lots_of_data);
		p.wait();
		assert_eq(p.read().length(), lots_of_data.length());
	}
	
	{
		process p;
		p.interact(true);
		assert(p.launch(CAT_STDIN));
		p.write("foo" LF);
		usleep(100*1000); // RACE
		assert_eq(p.read(), "foo" LF);
		assert(p.running());
		p.interact(false);
		usleep(100*1000); // RACE (this gets interrupted by SIGCHLD, but it's resumed)
		assert(!p.running());
	}
	
	//{
	//	string test_escape[] = {
	//		"DUMMY_NODE",
	//		"DUMMY_NODE",
	//		"a",
	//		"\"",
	//		"a b",
	//		"\"a b\"",
	//		" ",
	//		" a",
	//		"a ",
	//		"  ",
	//		" \" ",
	//		" \"\" ",
	//		" \" \"",
	//		"",
	//		"\"",
	//		"\\",
	//		"\\\"",
	//		"\\\\",
	//		"\\\\\"",
	//		"\\\\\\",
	//		"\\\\\\\"",
	//	};
	//	//this one is supposed to test that the arguments are properly quoted,
	//	// but there's no 'dump argv' program on windows (linux doesn't need it), so can't do it
	//}
}

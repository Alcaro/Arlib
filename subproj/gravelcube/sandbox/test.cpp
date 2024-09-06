#include "../arlib.h"
#include "sandbox.h"

#include <unistd.h> // usleep
#define TRUE "/bin/true"
#define FALSE "/bin/false"
#define ECHO "/bin/echo"
#define YES "/usr/bin/yes"
#define LF "\n"
#define ECHO_END LF
#define CAT_FILE "/bin/cat"
#define CAT_STDIN "/bin/cat"
#define CAT_STDIN_END ""

test("sandbox", "", "sandbox")
{
	if (RUNNING_ON_VALGRIND) test_skip_force("valgrind doesn't understand the sandbox");
	
	//use the global one instead here for no reason
	runloop* loop = runloop::global();
	//ugly, but the alternative is nesting lambdas forever or busywait. and I need a way to break it anyways
	int status;
	function<void(int lstatus)> break_runloop = [&](int lstatus) { status = lstatus; loop->exit(); };
	
	{
		sandproc p(loop);
		p.onexit(break_runloop);
		
		bool has_access_fail = false;
		// this will fail because can't access /lib64/ld-linux-x86-64.so.2
		// (or /bin/true or whatever - no point caring exactly what file makes it blow up)
		p.set_access_violation_cb([&](cstring path, bool write) { has_access_fail = true; });
		p.set_stdout(process::output::create_stdout());
		p.set_stderr(process::output::create_stderr());
		
//puts("");
//printf("%lu\n",time_us_ne());
		assert(p.launch(TRUE));
//printf("%lu\n",time_us_ne());
		loop->enter();
		
		assert(has_access_fail);
		assert_ne(status, 0);
	}
	
	{
		sandproc p(loop);
		p.onexit(break_runloop);
		
		p.set_access_violation_cb([&](cstring path, bool write) { puts(""+path); assert_unreachable(); });
		p.set_stdout(process::output::create_stdout());
		p.set_stderr(process::output::create_stderr());
		p.fs_grant_syslibs(TRUE);
		
//printf("%lu\n",time_us_ne());
		assert(p.launch(TRUE));
//printf("%lu\n",time_us_ne());
		loop->enter();
		
		assert_eq(status, 0);
	}
	
	{
		sandproc p(loop);
		p.onexit(break_runloop);
		
		p.set_access_violation_cb([&](cstring path, bool write) { puts(""+path); assert_unreachable(); });
		p.set_stdout(process::output::create_stdout());
		p.set_stderr(process::output::create_stderr());
		p.fs_grant_syslibs(FALSE);
		
//printf("%lu\n",time_us_ne());
		assert(p.launch(FALSE));
//printf("%lu\n",time_us_ne());
		loop->enter();
		
		assert_eq(status, 1); // ensure it can differentiate /bin/true and /bin/false, to prove it actually runs the program
	}
	
	//ensure pid namespace does not interfere with PR_SET_PDEATHSIG (verify with 'ps -ef | grep cat', or TR='strace -f')
	//{
	//	sandproc p(loop);
	//	
	//	p.set_access_violation_cb([&](cstring path, bool write) { assert_unreachable(); });
	//	p.fs_grant_syslibs("/bin/cat");
	//	p.fs_grant("/dev/pts/0");
	//	
	//	assert(p.launch("/bin/cat", "/dev/pts/0"));
	//	
	//	loop->set_timer_once(1000, [&]() { assert(p.running()); });
	//	loop->set_timer_once(10000, [&]() { abort(); });
	//	loop->enter();
	//}
}

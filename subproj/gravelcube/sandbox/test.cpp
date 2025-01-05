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

co_test("sandbox", "", "sandbox")
{
	if (RUNNING_ON_VALGRIND)
		test_skip_force("valgrind doesn't understand the sandbox");
	
	{
		sandproc p;
		
		bool has_access_fail = false;
		// this will fail because can't access /lib64/ld-linux-x86-64.so.2
		// (or /bin/true or whatever - no point caring exactly what file it complains about)
		p.set_access_violation_cb([&](cstring path, bool write) { has_access_fail = true; });
		
		assert(p.create({ .prog=TRUE, .fds={ 0, 1, 2 } }));
		int status = co_await p.wait();
		
		assert(has_access_fail);
		assert_ne(status, 0);
	}
	
	{
		sandproc p;
		
		p.set_access_violation_cb([&](cstring path, bool write) { puts(path); assert_unreachable(); });
		p.fs_grant_syslibs(TRUE);
		
		assert(p.create({ .prog=TRUE, .fds={ 0, 1, 2 } }));
		int status = co_await p.wait();
		
		assert_eq(status, 0);
	}
	
	{
		sandproc p;
		
		p.set_access_violation_cb([&](cstring path, bool write) { puts(path); assert_unreachable(); });
		p.fs_grant_syslibs(FALSE);
		
		assert(p.create({ .prog=FALSE, .fds={ 0, 1, 2 } }));
		int status = co_await p.wait();
		
		assert_eq(status, 1); // ensure it can differentiate /bin/true and /bin/false, to prove it actually runs the program
	}
	
	{
		sandproc p;
		
		bool has_access_fail = false;
		p.set_access_violation_cb([&](cstring path, bool write) { assert_eq(path, "/etc/issue"); has_access_fail = true; });
		p.fs_grant_syslibs(CAT_FILE);
		
		assert(p.create({ .argv={ CAT_FILE, "/etc/issue" }, .fds={ 0, 1, -1 } }));
		int status = co_await p.wait();
		
		assert(has_access_fail);
		assert_ne(status, 0);
	}
	
	{
		sandproc p;
		
		p.set_access_violation_cb([&](cstring path, bool write) { puts(path); assert_unreachable(); });
		p.fs_grant_syslibs(CAT_FILE);
		p.fs_grant("/etc/issue");
		
		process::pipe stdout = process::pipe::create();
		
		assert(p.create({ .argv={ CAT_FILE, "/etc/issue" }, .fds={ 0, stdout.wr, 2 } }));
		int status = co_await p.wait();
		
		assert_eq(status, 0);
		
		uint8_t buf[1024];
		int n = read(stdout.rd, buf, sizeof(buf));
		assert_eq(cstring(bytesr(buf, n)), file::readallt("/etc/issue"));
	}
	
	//ensure pid namespace does not interfere with PR_SET_PDEATHSIG (verify with 'ps -ef | grep cat', or TR='strace -f')
	//{
	//	sandproc p;
	//	
	//	p.set_access_violation_cb([&](cstring path, bool write) { assert_unreachable(); });
	//	p.fs_grant_syslibs("/bin/cat");
	//	p.fs_grant("/dev/pts/0");
	//	
	//	assert(p.create({ .argv={ "/bin/cat" }, .fds={ 0, 1, 2 } }));
	//	
	//	exit(0);
	//}
}

#include "process.h"

#ifdef __linux__
#include "os.h"
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <linux/sched.h>
#include <linux/wait.h>

bool process::set_fds(arrayvieww<int> fds, bool cloexec)
{
	if (fds.size() > INT_MAX) return false;
	
	bool ok = true;
	
	//probably doable with fewer dups, but yawn, don't care.
	for (size_t i=0;i<fds.size();i++)
	{
		while ((unsigned)fds[i] < i && fds[i] >= 0)
		{
			fds[i] = fcntl(fds[i], F_DUPFD_CLOEXEC, 0); // apparently this 0 is mandatory, despite docs implying it's unused and optional
			if (fds[i] < 0) ok = false;
		}
	}
	
	for (size_t i=0;i<fds.size();i++)
	{
		if (fds[i] >= 0)
		{
			if (fds[i] != (int)i)
			{
				if (dup3(fds[i], i, O_CLOEXEC) < 0)
				{
					ok = false;
					close(i);
				}
			}
			fcntl(i, F_SETFD, cloexec ? FD_CLOEXEC : 0);
		}
		if (fds[i] < 0) close(i);
	}
	
	return (close_range(fds.size(), UINT_MAX, 0) == 0 && ok);
}

process::strict_bool process::create(raw_params& param)
{
	// exec resets the child termination signal to SIGCHLD,
	// and kernel pretends to not know what the pidfd points to if the signal was zero
	// I could also use the vfork flags, but I can't quite determine what exactly is legal after vfork
	// I'd probably need to rewrite this function in assembly to make it safe (though I could put the pid==0 case in a C++ function)
#ifdef __x86_64__
	pid_t pid = syscall(__NR_clone, CLONE_PIDFD|SIGCHLD, NULL, (fd_raw_t*)&this->fd, NULL, NULL);
#endif
	// on FreeBSD, the equivalent is pdfork
	if (pid < 0)
		return false;
	if (pid == 0)
	{
		// in child process
		
		if (param.detach)
		{
			if (fork() != 0)
				_exit(0);
		}
		
		//WARNING:
		//fork(), POSIX.1-2008, http://pubs.opengroup.org/onlinepubs/9699919799/functions/fork.html
		//  If a multi-threaded process calls fork(), the new process shall contain a replica of the
		//  calling thread and its entire address space, possibly including the states of mutexes and
		//  other resources. Consequently, to avoid errors, the child process may only execute
		//  async-signal-safe operations until such time as one of the exec functions is called.
		//In particular, malloc must be avoided.
		
		fd_t devnull;
		for (size_t i=0;i<param.fds.size();i++)
		{
			if (param.fds[i] == -1)
			{
				if (!devnull.valid())
					devnull = fd_t::create_devnull();
				param.fds[i] = devnull;
			}
		}
		if (!set_fds(param.fds))
			_exit(EXIT_FAILURE);
		execve(param.prog, (char**)param.argv, (char**)param.envp); // why are these not properly const declared
		while (true)
			_exit(EXIT_FAILURE);
	}
	if (param.detach)
	{
		process_wait_sync(this->fd);
		return true;
	}
	m_pid = pid;
	return true;
}

bool process::process_try_wait(fd_t& fd, int& ret, bool async)
{
	siginfo_t si;
	si.si_pid = 0; // unnecessary on Linux, needed on other Unix
	// todo: delete cast, and include of <linux/wait.h>, when dropping ubuntu 22.04
	waitid((idtype_t)P_PIDFD, (int)fd, &si, WEXITED|WSTOPPED|WCONTINUED|(async?WNOHANG:0));
	if (si.si_pid != 0 && (si.si_code == CLD_EXITED || si.si_code == CLD_DUMPED || si.si_code == CLD_KILLED))
	{
		fd.close();
		ret = si.si_status;
		return true;
	}
	return false;
}

int process::process_wait_sync(fd_t& fd)
{
	while (true)
	{
		int ret;
		if (process_try_wait(fd, ret, false))
			return ret;
	}
}

async<int> process::wait()
{
	if (!fd.valid())
		co_return -1;
	while (true)
	{
		co_await runloop2::await_read(fd);
		int ret;
		if (process_try_wait(fd, ret, true))
		{
			m_pid = 0;
			co_return ret;
		}
	}
}

void process::terminate()
{
	if (!fd.valid())
		return;
	syscall(SYS_pidfd_send_signal, (int)fd, SIGKILL, nullptr, 0);
	process_wait_sync(fd);
	m_pid = 0;
}

process::pipe process::pipe::create()
{
	int fds[2] = { -1, -1 };
	if (pipe2(fds, O_CLOEXEC) != 0)
		debug_fatal("pipe2() failed\n");
	return { fds[0], fds[1] };
}

process::strict_bool process::create(params& param)
{
	array<const char *> argv;
	for (const string& s : param.argv)
		argv.append(s);
	if (!param.argv)
		argv.append(param.prog);
	argv.append(NULL);
	
	array<const char *> envp;
	for (const string& s : param.envp)
		envp.append(s);
	envp.append(NULL);
	
	if (param.fds.size() < 3)
	{
		if (param.fds.size() == 0)
			param.fds.append(0);
		if (param.fds.size() == 1)
			param.fds.append(1);
		param.fds.append(2);
	}
	
	raw_params rparam;
	string prog = find_prog(param.prog ? param.prog : param.argv[0]);
	rparam.prog = prog;
	rparam.argv = argv.ptr();
	if (param.envp)
		rparam.envp = envp.ptr();
	else
		rparam.envp = __environ;
	rparam.fds = param.fds;
	rparam.detach = param.detach;
	
	return create(rparam);
}

string process::find_prog(cstring prog)
{
	if (prog.contains("/")) return prog;
	array<string> paths = string(getenv("PATH")).split(":");
	for (string& path : paths)
	{
		string pathprog = path+"/"+prog;
		if (access(pathprog, X_OK) == 0) return pathprog;
	}
	return "";
}

co_test("process", "array,string", "process")
{
	if (RUNNING_ON_VALGRIND)
		test_skip_force("prints valgrind heap summary a few times");
	{
		process p;
		assert(p.create({ "true", { "/bin/true" } }));
		assert_eq(co_await p.wait(), 0);
	}
	{
		process p;
		assert(p.create({ "false", { "/bin/false" } }));
		assert_eq(co_await p.wait(), 1);
	}
	{
		process p;
		assert(p.create({ "program that doesn't exist", { "a" } }));
		assert_eq(co_await p.wait(), EXIT_FAILURE);
	}
	{
		process().create({ .prog="program that doesn't exist", .argv={ "a" }, .detach=true });
	}
}
#endif

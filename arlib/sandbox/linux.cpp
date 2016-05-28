#ifdef __linux__
#include "sandbox.h"
#include "../thread/atomic.h"
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <fcntl.h>
#include <sys/wait.h> 

void sandbox_lockdown();

//#error fill in the sandbox

struct sandbox::impl {
	int childpid; // pid, or 0 if this is the child
	sandbox::impl* childdat;
	
#define CH_FREE 0
#define CH_LOCKED 1
#define CH_SLEEPER 2 // someone is futexing on this
	int channels[8]; // futex
	//they could be merged to one int (16 bits), but there's no point saving 56 bytes. the shmem will be rounded to 4K anyways
	
	//0 - parent's turn
	//1 - child's turn
	//also used during initialization, with same values
	int sh_futex; // futex
	int sh_fd[8];
	void* sh_ptr[8];
	size_t sh_len[8];
	
	void(*setup)(sandbox* box);
	void(*run)(sandbox* box);
};

//waits while *uaddr == val
//non-private because this goes on multiple address spaces
static int futex_wait(int * uaddr, int val, const struct timespec * timeout = NULL)
{
	return syscall(__NR_futex, uaddr, FUTEX_WAIT, val, timeout);
}
static int futex_wake(int * uaddr)
{
	return syscall(__NR_futex, uaddr, FUTEX_WAKE, 1);
}
//static int futex_wake_all(int * uaddr)
//{
//	return syscall(__NR_futex, uaddr, FUTEX_WAKE, INT_MAX);
//}

static void futex_wait_while_eq(int * uaddr, int val)
{
	while (lock_read(uaddr)==val) futex_wait(uaddr, val);
}
static void futex_set_and_wake(int * uaddr, int val)
{
	lock_write(uaddr, val);
	futex_wake(uaddr);
}

void sandbox::enter(int argc, char** argv)
{
	if (strcmp(argv[0], "[Arlib sandboxed process]")!=0) return;
	
	sandbox::impl* box = (sandbox::impl*)mmap(NULL, sizeof(sandbox::impl), PROT_READ|PROT_WRITE, MAP_SHARED, atoi(argv[1]), 0);
	
	futex_set_and_wake(&box->sh_futex, 1);
	
	sandbox boxw(box);
	if (box->setup) box->setup(&boxw);
	
	sandbox_lockdown();
	
	box->run(&boxw);
	exit(0);
}


static int tmp_create()
{
	const char * paths[] = { "/run/", "/tmp/", "/home/", "/", NULL };
	for (int i=0;paths[i];i++)
	{
		int fd = open(paths[i], O_TMPFILE|O_EXCL|0666);
		if (fd >= 0) return fd;
	}
	return -1;
}

sandbox* sandbox::create(const params* param)
{
	sandbox::impl* par = (sandbox::impl*)malloc(sizeof(sandbox::impl));
	int shmfd = tmp_create();
	ftruncate(shmfd, sizeof(sandbox::impl));
	sandbox::impl* chi = (sandbox::impl*)mmap(NULL, sizeof(sandbox::impl), PROT_READ|PROT_WRITE, MAP_SHARED, shmfd, 0);
	memset(par, 0, sizeof(*par));
	memset(chi, 0, sizeof(*chi));
	
	par->childdat = chi;
	chi->setup = param->setup;
	chi->run = param->run;
	for (int i=0;i<8;i++) par->sh_fd[8] = -1;
	for (int i=0;i<param->n_channel;i++)
	{
		chi->channels[i] = 1;
	}
	for (int i=0;i<param->n_shmem;i++)
	{
		par->sh_fd[i] = tmp_create();
		chi->sh_fd[i] = par->sh_fd[i];
	}
	
	int childpid = fork();
	if (childpid < 0)
	{
		close(shmfd);
		return NULL;
	}
	if (childpid == 0)
	{
		char shmfd_s[16];
		sprintf(shmfd_s, "%i", shmfd);
		const char * argv[3] = { "[Arlib sandboxed process]", shmfd_s, NULL };
		
		execv("/proc/self/exe", (char**)argv);
		_exit(0);
	}
	if (childpid > 0)
	{
		futex_wait_while_eq(&chi->sh_futex, 0);
		lock_write(&chi->sh_futex, 0);
		
		par->childpid = childpid;
		return new sandbox(par);
	}
	return NULL; // unreachable, but gcc doesn't know this
}

void sandbox::wait(int chan)
{
	if (lock_cmpxchg(&m->channels[chan], CH_FREE, CH_LOCKED) == CH_FREE) return;
	
	while (true)
	{
		//already did a barrier above, don't need another one
		int prev = lock_xchg_loose(&m->channels[chan], CH_SLEEPER);
		if (prev == CH_FREE) return;
		futex_wait(&m->channels[chan], CH_SLEEPER);
	}
}

bool sandbox::try_wait(int chan)
{
	int old = lock_cmpxchg(&m->channels[chan], 0, 1);
	return (old == 0);
}

void sandbox::release(int chan)
{
	int old = lock_xchg(&m->channels[chan], 0);
	if (LIKELY(old != CH_SLEEPER)) return;
	futex_wake(&m->channels[chan]);
}

void* sandbox::shalloc(int index, size_t bytes)
{
	if (m->sh_ptr[index]) munmap(m->sh_ptr[index], m->sh_len[index]);
	m->sh_ptr[index] = NULL;
	
	void* ret;
	if (!m->childpid) // child, tell parent we're unmapped (if shrinking, we risk SIGBUS in child if we don't wait)
	{
		futex_set_and_wake(&m->sh_futex, 1);
	}
	if (m->childpid) // parent, resize and map
	{
		futex_wait_while_eq(&m->childdat->sh_futex, 0);
		
		ftruncate(m->sh_fd[index], bytes);
		
		ret = mmap(NULL, bytes, PROT_READ|PROT_WRITE, MAP_SHARED, m->sh_fd[index], 0);
		if (ret == MAP_FAILED) ret = NULL;
		
		futex_set_and_wake(&m->childdat->sh_futex, (ret ? 2 : 0));
	}
	if (!m->childpid) // child, map
	{
		futex_wait_while_eq(&m->sh_futex, 1);
		
		if (m->sh_futex == 0) return NULL;
		
		ret = mmap(NULL, bytes, PROT_READ|PROT_WRITE, MAP_SHARED, m->sh_fd[index], 0);
		if (ret == MAP_FAILED) ret = NULL;
		
		futex_set_and_wake(&m->sh_futex, (ret ? 3 : 0));
	}
	if (m->childpid) // parent, check that child succeeded
	{
		futex_wait_while_eq(&m->childdat->sh_futex, 2);
		
		if (m->childdat->sh_futex == 0) return NULL;
		
		futex_set_and_wake(&m->childdat->sh_futex, 0);
	}
	
	m->sh_ptr[index] = ret;
	m->sh_len[index] = bytes;
	return ret;
}

sandbox::~sandbox()
{
	//this can blow up if our caller has a SIGCHLD handler that discards everything, and the PID was reused
	kill(m->childpid, SIGKILL);
	waitpid(m->childpid, NULL, WNOHANG);
	
	for (int i=0;i<8;i++)
	{
		if (m->sh_ptr[i]) munmap(m->sh_ptr[i], m->sh_len[i]);
		if (m->sh_fd[i]>=0) close(m->sh_fd[i]);
	}
	munmap(m->childdat, sizeof(sandbox::impl));
	free(m);
}
#endif

#ifdef __linux__
#include "sandbox.h"
#include "../thread/atomic.h"
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/syscall.h>
#include <linux/futex.h>

void sandbox_lockdown();

#error fill in the sandbox
#error replace with open(O_TMPFILE, "/run") (on failure, use /tmp instead)
#error fd passing via unix socket, as usual

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
	int sh_newid; // shmid
	void* sh_ptr[8];
	
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
	if (argc!=2 || strcmp(argv[0], "[Arlib sandboxed process]")!=0) return;
	
	sandbox::impl* box = (sandbox::impl*)shmat(atoi(argv[1]), NULL, 0);
	
	futex_set_and_wake(&box->sh_futex, 1);
	
	sandbox boxw(box);
	if (box->setup) box->setup(&boxw);
	
	sandbox_lockdown();
	
	box->run(&boxw);
	exit(0);
}


sandbox* sandbox::create(const params* param)
{
	for (int i=0;i<10000;i++)
	{
		struct shmid_ds g;
		int id = shmctl(i, SHM_STAT, &g);
		if (id >= 0 && g.shm_segsz == sizeof(sandbox::impl) && g.shm_nattch == 0)
		{
			printf("shmgc: %i %i %lu %lu\n", id, i, g.shm_segsz, g.shm_nattch);
			shmctl(id, IPC_RMID, NULL);
		}
	}
	
	sandbox::impl* par = (sandbox::impl*)malloc(sizeof(sandbox::impl));
	int shmid = shmget(IPC_PRIVATE, sizeof(sandbox::impl), IPC_CREAT | 0666);
	sandbox::impl* chi = (sandbox::impl*)shmat(shmid, NULL, 0);
	memset(par, 0, sizeof(*par));
	memset(chi, 0, sizeof(*chi));
	
	par->childdat = chi;
	chi->setup = param->setup;
	chi->run = param->run;
	for (int i=0;i<8;i++)
	{
		chi->channels[i] = 1;
	}
	
	int childpid = fork();
	if (childpid < 0)
	{
		shmdt(chi);
		return NULL;
	}
	if (childpid == 0)
	{
		char shmid_s[16];
		sprintf(shmid_s, "%i", shmid);
		const char * argv[] = {"[Arlib sandboxed process]", shmid_s, NULL};
		execv("/proc/self/exe", (char**)argv);
		_exit(0);
	}
	if (childpid > 0)
	{
		futex_wait_while_eq(&chi->sh_futex, 0);
		lock_write(&chi->sh_futex, 0);
		
		par->childpid = childpid;
		shmctl(shmid, IPC_RMID, NULL);
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
	if (m->sh_ptr[index]) shmdt(m->sh_ptr[index]);
	
	void* ret;
	if (m->childpid) // parent
	{
		int shmid = shmget(IPC_PRIVATE, bytes, IPC_CREAT | 0666);
		ret = shmat(shmid, NULL, 0);
		
		m->childdat->sh_newid = shmid;
		
		futex_set_and_wake(&m->childdat->sh_futex, 1);
		futex_wait_while_eq(&m->childdat->sh_futex, 1);
		
		//fut = 2
		
		shmctl(shmid, IPC_RMID, NULL);
		
		if (!m->childdat->sh_newid)
		{
			shmdt(ret);
			ret = NULL;
		}
		lock_write_loose(&m->childdat->sh_futex, 0);
	}
	else // child
	{
		futex_wait_while_eq(&m->sh_futex, 0);
		
		ret = shmat(m->sh_newid, NULL, 0);
		if (!ret) m->sh_newid = 0;
		
		futex_set_and_wake(&m->sh_futex, 0);
	}
	
	m->sh_ptr[index] = ret;
	return ret;
}

sandbox::~sandbox()
{
	//this can blow up if our caller has a SIGCHLD handler that discards everything, and the PID was reused
	kill(m->childpid, SIGKILL);
	waitpid(m->childpid, NULL, WNOHANG);
	
	for (int i=0;i<8;i++)
	{
		if (m->sh_ptr[i]) shmdt(m->sh_ptr[i]);
	}
	shmdt(m->childdat);
	free(m);
}
#endif

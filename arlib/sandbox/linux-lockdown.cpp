#ifdef __linux__
#include "sandbox.h"
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/shm.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/syscall.h>
#include <linux/futex.h>
       #include <sys/types.h>
       #include <sys/stat.h>
       #include <fcntl.h>


#include <sys/types.h>
#include <dirent.h>

#include <sys/types.h>
#include <sys/wait.h> 


static void close_fds()
{
	//from http://www.opensource.apple.com/source/sudo/sudo-46/src/closefrom.c, license BSD-2
	struct dirent *dent;
	DIR* dirp;
	char* endp;
	long fd;
	
	/* Use /proc/self/fd directory if it exists. */
	if ((dirp = opendir("/proc/self/fd")) != NULL) {
		while ((dent = readdir(dirp)) != NULL) {
			fd = strtol(dent->d_name, &endp, 10);
			if (dent->d_name != endp && *endp == '\0' &&
				fd >= 0 && fd < INT_MAX && fd != dirfd(dirp))
				(void) close((int) fd);
		}
		(void) closedir(dirp);
	}
	else
	{
		_exit(0);
	}
}

void sandbox_lockdown()
{
	prctl(PR_SET_DUMPABLE, 0);
	prctl(PR_SET_TSC, PR_TSC_SIGSEGV);
	prctl(PR_SET_PDEATHSIG, SIGKILL);
	
	//close_fds();
}
#endif

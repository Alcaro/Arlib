#include "thread.h"
#if defined(__unix__)
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

//list of synchronization points: http://pubs.opengroup.org/onlinepubs/009695399/basedefs/xbd_chap04.html#tag_04_10

struct threaddata_pthread {
	function<void()> func;
};
static void * threadproc(void * userdata)
{
	struct threaddata_pthread * thdat=(struct threaddata_pthread*)userdata;
	thdat->func();
	free(thdat);
	return NULL;
}

void thread_create(function<void()> start)
{
	struct threaddata_pthread * thdat=malloc(sizeof(struct threaddata_pthread));
	thdat->func=start;
	pthread_t thread;
	if (pthread_create(&thread, NULL, threadproc, thdat)) abort();
	pthread_detach(thread);
}

//unsigned int thread_num_cores()
//{
//	//for more OSes: https://qt.gitorious.org/qt/qt/source/HEAD:src/corelib/thread/qthread_unix.cpp#L411, idealThreadCount()
//	//or http://stackoverflow.com/questions/150355/programmatically-find-the-number-of-cores-on-a-machine
//	return sysconf(_SC_NPROCESSORS_ONLN);
//}
#endif

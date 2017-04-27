#include "global.h"
#include "array.h"

#ifdef __linux__
#ifdef ARLIB_THREAD
#include "thread.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

class fd_mon_t {
	struct node {
		int fd;
		function<void(int)> action;
	};
	array<node> read_act;
	array<node> write_act;
	//TODO: map<int, function<void(int)>> on_read;
	
	int selfpipe[2];
	
	mutex mut;
	bool initialized;
	
	void process()
	{
		while (true)
		{
			fd_set readfds;
			fd_set writefds;
			FD_ZERO(&readfds);
			FD_ZERO(&writefds);
			
			synchronized(mut)
			{
				for (node& n : read_act)  FD_SET(n.fd, &readfds);
				for (node& n : write_act) FD_SET(n.fd, &writefds);
			}
			
			select(FD_SETSIZE, &readfds, &writefds, NULL, NULL);
			
			synchronized(mut)
			{
				for (node& n : read_act)
				{
					if (FD_ISSET(n.fd, &readfds))
					{
						n.action(n.fd);
					}
				}
				for (node& n : write_act)
				{
					if (FD_ISSET(n.fd, &writefds))
					{
						n.action(n.fd);
					}
				}
				
			}
		}
	}
	
	void initialize()
	{
		if (initialized) return;
		initialized = true;
		
		if (pipe(selfpipe) < 0) abort();
		monitor_nosync(selfpipe[0], [](int){}, NULL);
		
		thread_create(bind_this(&fd_mon_t::process));
	}
	
	void monitor_nosync(int fd, function<void(int)> on_read, function<void(int)> on_write)
	{
		size_t idx;
		
		for (idx=0;idx<read_act.size() && read_act[idx].fd != fd;idx++) {}
		read_act[idx].fd = fd;
		read_act[idx].action = on_read;
		if (!on_read) read_act.remove(idx);
		
		for (idx=0;idx<write_act.size() && write_act[idx].fd != fd;idx++) {}
		write_act[idx].fd = fd;
		write_act[idx].action = on_write;
		if (!on_write) write_act.remove(idx);
		
		//wake up the thread, so it adds the new fd
		//discard return value, failure probably means thread is busy and pipe is full
		if (write(selfpipe[1], "", 1) < 0) {}
	}
	
public:
	void monitor(int fd, function<void(int)> on_read, function<void(int)> on_write)
	{
		synchronized(mut)
		{
			initialize();
			monitor_nosync(fd, on_read, on_write);
		}
	}
};

//there's no need for more than one of these
static fd_mon_t fd_mon;
void fd_monitor(int fd, function<void(int)> on_read, function<void(int)> on_write)
{
	fd_mon.monitor(fd, on_read, on_write);
}
#endif
#endif

#include "global.h"

#if defined(__linux__) && defined(ARLIB_THREAD)
#include "array.h"
#include "set.h"
#include "thread.h"

#include <sys/epoll.h>
#include <unistd.h>
#define RD_EV (EPOLLIN |EPOLLRDHUP|EPOLLHUP|EPOLLERR)
#define WR_EV (EPOLLOUT|EPOLLRDHUP|EPOLLHUP|EPOLLERR)

class fd_mon_t : nocopy {
	map<int, function<void(int)>> read_act;
	map<int, function<void(int)>> write_act;
	
	int epoll_fd = -1;
	mutex_rec mut;
	
	void process()
	{
		while (true)
		{
			epoll_event ev[16];
			int nev = epoll_wait(epoll_fd, ev, 16, -1);
			
			if (nev)
			{
				synchronized(mut)
				{
					for (int i=0;i<nev;i++)
					{
						if (ev[i].events & RD_EV)  read_act.get_or(ev[i].data.fd, NULL)(ev[i].data.fd);
						if (ev[i].events & WR_EV) write_act.get_or(ev[i].data.fd, NULL)(ev[i].data.fd);
					}
				}
			}
		}
	}
	
	void monitor_raw(int fd, function<void(int)> on_read, function<void(int)> on_write)
	{
		if (on_read) read_act.insert(fd, on_read);
		else read_act.remove(fd);
		
		if (on_write) write_act.insert(fd, on_write);
		else write_act.remove(fd);
		
		epoll_event ev;
		ev.events = (on_read ? RD_EV : 0) | (on_write ? WR_EV : 0);
		ev.data.fd = fd;
		if (ev.events)
		{
			epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev); // one of these two will fail (or do nothing), we'll ignore that
			epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
		}
		else
		{
			epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
		}
	}
	
	void initialize()
	{
		if (epoll_fd >= 0) return;
		
		epoll_fd = epoll_create1(EPOLL_CLOEXEC);
		thread_create(bind_this(&fd_mon_t::process));
	}
	
public:
	void monitor(int fd, function<void(int)> on_read, function<void(int)> on_write)
	{
		synchronized(mut)
		{
			//at this point, process() is known to not be in a handler (unless it's recursive), or it would've held the lock
			initialize();
			
			monitor_raw(fd, on_read, on_write);
		}
	}
	
	~fd_mon_t() { if (epoll_fd != -1) close(epoll_fd); }
};

//there's no need for more than one of these
static fd_mon_t fd_mon;
void fd_monitor(int fd, function<void(int)> on_read, function<void(int)> on_write)
{
	fd_mon.monitor(fd, on_read, on_write);
}
#endif

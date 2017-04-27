#ifdef __linux__
#include "sandbox.h"
#include "../process.h"
#include "../test.h"
#include "../file.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <sys/wait.h>

#include "linux-sand-internal.h"

class sand_broker {
public:
	pid_t pid;
	array<int> fds;
	int exitcode;
	
	void on_readable(int sock)
	{
		struct broker_req req;
		ssize_t req_sz = recv(sock, &req, sizeof(req), MSG_DONTWAIT);
		if (req_sz==-1 && errno==EAGAIN) return;
		else if (req_sz == 0)
		{
			//TODO: remove socket but don't kill
		}
		else if (req_sz != sizeof(req))
		{
			terminate(); // no strange messages allowed
			return;
		}
		
		switch (req.type)
		{
		case br_open:
		{
			//TODO: tighten this up
			int fd = open(req.path, req.flags[0], req.flags[1]);
			broker_rsp rsp = { br_open, fd<0 ? errno : 0 };
			if (send_fd(sock, &rsp, sizeof(rsp), MSG_DONTWAIT|MSG_NOSIGNAL, fd) <= 0)
			{
				//full buffer means misbehaving child, closed socket other error or child closed
				//none of which should happen, so kill it
				terminate();
			}
			close(fd);
			break;
		}
		default:
			terminate(); // invalid request means child is doing something stupid
			break;
		}
	}
	
	void cleanup()
	{
		for (int fd : fds)
		{
			close(fd);
		}
		fds.reset();
	}
	
	void terminate()
	{
		kill(pid, SIGKILL);
		cleanup();
		
		int x;
		waitpid(pid,&x,0);
		
		pid = -1;
	}
	
	void init(int pid, int sock)
	{
		this->pid = pid;
		fd_monitor(sock, bind_this(&sand_broker::on_readable), NULL);
	}
	
	void wait()
	{
		while (lock_read(&pid) != -1)
		{
			usleep(10000);
		}
	}
};

static sand_broker box;
void sand_do_the_thing(int pid, int sock)
{
	box.init(pid, sock);
	box.wait();
}
#endif

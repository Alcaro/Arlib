#ifdef __linux__
#ifndef ARLIB_TEST
#include "sandbox.h"
#include "../process.h"
#include "../test.h"
#include "../file.h"
#include "../set.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <sys/wait.h>

#include "internal-linux-sand.h"

#include <sys/syscall.h>
#include <linux/memfd.h>
static inline int memfd_create(const char * name, unsigned int flags) { return syscall(__NR_memfd_create, name, flags); }

class sand_fs : nocopy {
	enum type_t { ty_error, ty_native, ty_tmp };
	struct mount {
		type_t type;
		
		union {
			#define SAND_ERRNO_MASK 0xFFF
			#define SAND_ERRNO_NOISY 0x1000
			int e_error;
			int n_fd;
			//ty_tmp's fd is in tmpfiles, keyed by full path
		};
		int numwrites; // decreased every time a write is done on native, or a file is created on tmp
	};
	map<string, mount> mounts;
	map<string, int> tmpfiles;
	
public:
	//child path (can be stuff like /@CWD/), parent path (must exist), max number of times files may be opened for writing
	//NOTE: The filename component, if any, must be same between child/parent paths.
	void grant_native_redir(string cpath, string ppath, int max_write = 0)
	{
		mount& m = mounts.insert(cpath);
		
		string cend = cpath.rsplit<1>("/")[1];
		array<string> pp = ppath.rsplit<1>("/");
		string pend = pp[1];
		if (cend != pend) abort();
		
		m.type = ty_native;
		m.n_fd = open(pp[0]+"/", O_DIRECTORY|O_PATH); // +"/" in case ppath is "/", so pp[0] is empty
		m.numwrites = max_write;
		
		if (m.n_fd < 0)
		{
			m.type = ty_error;
			m.e_error = errno;
		}
	}
	void grant_native(string path, int max_write = 0)
	{
		grant_native_redir(path, path, max_write);
	}
	void grant_tmp(string cpath, int max_size)
	{
		mount& m = mounts.insert(cpath);
		
		m.type = ty_tmp;
		m.numwrites = max_size;
	}
	void grant_errno(string cpath, int error, bool noisy)
	{
		mount& m = mounts.insert(cpath);
		
		m.type = ty_error;
		m.e_error = error | (noisy ? SAND_ERRNO_NOISY : 0);
	}
	
	sand_fs()
	{
		grant_errno("/", EACCES, true);
	}
	
	~sand_fs()
	{
		for (auto& m : mounts)
		{
			if (m.value.type == ty_native) close(m.value.n_fd);
		}
		for (auto& m : tmpfiles)
		{
			close(m.value);
		}
	}
	
	//TODO
	//function<void(const char * path, bool write)> report_access_violation;
	void report_access_violation(cstring path, bool write)
	{
		puts((cstring)"sandbox: denied " + (write?"writing":"reading") + " " + path);
	}
	
	//calls report_access_violation if needed
	int child_file(cstring pathname, broker_req_t op, int flags, mode_t mode)
	{
		bool is_write;
		if (op == br_open)
		{
			//block unfamiliar or unusable flags
			//intentionally rejected flags: O_DIRECT, O_DSYNC, O_PATH, O_SYNC, __O_TMPFILE
			if (flags & ~(O_ACCMODE|O_APPEND|O_ASYNC|O_CLOEXEC|O_CREAT|O_DIRECTORY|O_EXCL|
						  O_LARGEFILE|O_NOATIME|O_NOCTTY|O_NOFOLLOW|O_NONBLOCK|O_TRUNC))
			{
				errno = EINVAL;
				return -1;
			}
			
			if ((flags&O_ACCMODE) == O_ACCMODE)
			{
				errno = EINVAL;
				return -1;
			}
			
			is_write = ((flags&O_ACCMODE) != O_RDONLY) || (flags&O_CREAT) || (flags&O_TRUNC);
		}
		else if (op == br_unlink) is_write = true;
		else if (op == br_access) is_write = false;
		else
		{
			errno = EINVAL;
			return -1;
		}
		
		if (pathname[0]!='/' ||
		    pathname.contains("/./") || pathname.contains("/../") ||
		    pathname.endswith("/.") || pathname.endswith("/.."))
		{
			report_access_violation(pathname, is_write);
			errno = EACCES;
			return -1;
		}
		
		bool exact_path = false;
		
//puts("open "+pathname);
		mount m;
		size_t mlen = 0;
		for (auto& miter : mounts)
		{
			if (miter.key[0]!='/') abort();
//puts("  mount "+miter.key);
			if (miter.key.length() <= mlen) continue;
			bool use;
			if (miter.key.endswith("/"))
			{
				if ((pathname+"/")==miter.key)
				{
					exact_path = true;
					use = true;
				}
				else use = (pathname.startswith(miter.key));
			}
			else
			{
				use = (pathname == miter.key); // file mount
			}
			if (use)
			{
//puts("    yes");
				m = miter.value;
				mlen = miter.key.length();
				while (miter.key[mlen-1]!='/') mlen--;
			}
		}
		if (!mlen) abort();
//puts("  "+pathname+" "+tostring(mlen));
		
		
		switch (m.type)
		{
		case ty_error:
			if (m.e_error & SAND_ERRNO_NOISY)
			{
				report_access_violation(pathname, is_write);
			}
			errno = m.e_error&SAND_ERRNO_MASK;
			return -1;
			
		case ty_native:
		{
			if (is_write)
			{
				if (m.numwrites == 0)
				{
					report_access_violation(pathname, true);
					errno = EACCES;
					return -1;
				}
				m.numwrites--;
			}
			
			cstring relpath;
			if (mlen > pathname.length()) relpath = "."; // open("/usr/include/") when that's a mountpoint
			else relpath = pathname.csubstr(mlen, ~0);
			
			if (op == br_open) return openat(m.n_fd, relpath, flags|O_CLOEXEC|O_NOCTTY, mode);
			if (op == br_unlink) return unlinkat(m.n_fd, relpath, 0);
			if (op == br_access) return faccessat(m.n_fd, relpath, flags, 0);
			abort();
		}
		case ty_tmp:
			if (op == br_open)
			{
				int fd = tmpfiles.get_or(pathname, -1);
				if (fd < 0 && is_write)
				{
					if (m.numwrites == 0)
					{
						report_access_violation(pathname, true);
						errno = ENOMEM;
						return -1;
					}
					m.numwrites--;
					
					fd = memfd_create(pathname, MFD_CLOEXEC);
					tmpfiles.insert(pathname, fd);
				}
				//unshare file position
				return open("/proc/self/fd/"+tostring(fd), (flags|O_CLOEXEC|O_NOCTTY)&~O_EXCL);
			}
			if (op == br_unlink)
			{
				int fd = tmpfiles.get_or(pathname, -1);
				if (fd >= 0)
				{
					tmpfiles.remove(pathname);
					close(fd);
					return 0;
				}
				else
				{
					errno = ENOENT;
					return -1;
				}
			}
			if (op == br_access)
			{
				if (tmpfiles.contains(pathname) || exact_path)
				{
					return 0;
				}
				else
				{
					errno = ENOENT;
					return -1;
				}
			}
			abort();
		default: abort();
		}
	}
};


class sand_broker : nocopy {
public:
	pid_t pid;
	array<int> socks; // TODO: set<int>
	int exitcode;
	
	sand_fs fs;
	
	void watch_add(int sock)
	{
		fd_monitor(sock, bind_this(&sand_broker::on_readable), NULL);
		socks.append(sock);
	}
	
	void watch_del(int sock)
	{
		fd_monitor(sock, NULL, NULL);
		
		size_t i=0;
		while (true)
		{
			if (socks[i]==sock)
			{
				socks.remove(i);
				break;
			}
			i++;
			if (i == socks.size())
			{
				abort(); // unwatching something not in the watchlist = sandbox bug = panic
			}
		}
	}
	
	void send_rsp(int sock, broker_rsp* rsp, int fd)
	{
		if (send_fd(sock, rsp, sizeof(*rsp), MSG_DONTWAIT|MSG_NOSIGNAL, fd) <= 0)
		{
			//full buffer means misbehaving child or child exited
			//none of which should happen, so kill it
			//(okay, it could hit if a threaded child calls open() and exit() simultaneously)
			//(but that's not noticable unless they've got a parent process and let's just assume no such thing is gonna happen.)
			terminate();
		}
	}
	
	void on_readable(int sock)
	{
puts("recv");
		struct broker_req req;
		ssize_t req_sz = recv(sock, &req, sizeof(req), MSG_DONTWAIT);
		if (req_sz==-1 && errno==EAGAIN) return;
		else if (req_sz == 0)
		{
			//closed socket? child probably exited
			watch_del(sock);
			if (!socks.size())
			{
				//if that was the last one, either the entire child tree is terminated or it's misbehaving
				//in both cases, kill it
				terminate();
			}
			return;
		}
		else if (req_sz != sizeof(req))
		{
			terminate(); // no strange messages allowed
			return;
		}
puts("switch");
		
		broker_rsp rsp = { req.type };
		int fd = -1;
		bool close_fd = true;
		
		switch (req.type)
		{
		case br_nop:
		{
puts("nop");
			return; // don't send any response
		}
		case br_ping:
		{
puts("ping");
			break; // pings just return the pre-initialized struct
		}
		case br_open:
		case br_unlink:
		case br_access:
		{
puts((string)"open "+req.path);
			close_fd = true;
			fd = fs.child_file(req.path, req.type, req.flags[0], req.flags[1]);
//puts((string)req.path+" "+tostring(req.type)+": "+tostring(fd)+" "+tostring(errno));
			if (fd<0) rsp.err = errno;
puts("done");
			break;
		}
		case br_get_emul:
		{
			int preloader_fd();
			fd = preloader_fd();
			close_fd = false;
			break;
		}
		case br_fork:
		{
			int socks[2];
			if (socketpair(AF_UNIX, SOCK_SEQPACKET|SOCK_CLOEXEC, 0, socks)<0)
			{
				socks[0] = -1;
				socks[1] = -1;
			}
			
			fd = socks[1];
			watch_add(socks[0]);
			break;
		}
		default:
puts("kill");
			terminate(); // invalid request means child is doing something stupid
			return;
		}
puts("return "+tostring(fd));
		send_rsp(sock, &rsp, fd>0 ? fd : -1);
		if (fd>0 && close_fd) close(fd);
puts("done");
	}
	
	void cleanup()
	{
		for (int sock : socks)
		{
			fd_monitor(sock, NULL, NULL);
			close(sock);
		}
		socks.reset();
	}
	
	void terminate()
	{
		if (pid<0) return; // do NOT call kill(-1, SIGKILL) under ANY circumstances, it just reboots your session
		
		kill(pid, SIGKILL);
		cleanup();
		
		waitpid(pid, &exitcode, 0);
		lock_write_rel(&pid, -1);
	}
	
	
	
	
	
	
	
	
	
	
	
	
	
	//TODO
	void init(int pid, int sock)
	{
		this->pid = pid;
		watch_add(sock);
	}
	
	void wait()
	{
		while (lock_read_acq(&pid) != -1)
		{
			usleep(10000);
		}
	}
};

static sand_broker box;
void sand_do_the_thing(int pid, int sock)
{
	box.init(pid, sock);
	//TODO: use an ordered map instead
	box.fs.grant_native_redir("/@CWD/", "./", 10);
	box.fs.grant_native("/lib64/ld-linux-x86-64.so.2");
	box.fs.grant_native("/usr/bin/gcc");
	box.fs.grant_native("/usr/bin/as");
	box.fs.grant_native("/usr/bin/ld");
	//box.fs.grant_native("/usr/lib/gcc/");
	box.fs.grant_native("/usr/lib/");
	box.fs.grant_native("/lib/");
	box.fs.grant_native("/dev/urandom");
	box.fs.grant_errno("/etc/ld.so.nohwcap", ENOENT, false);
	box.fs.grant_errno("/etc/ld.so.preload", ENOENT, false);
	box.fs.grant_native("/etc/ld.so.cache");
	box.fs.grant_errno("/usr", ENOENT, false);
	box.fs.grant_errno("/usr/local/include/", ENOENT, false);
	box.fs.grant_native("/usr/include/");
	box.fs.grant_errno("/usr/x86_64-linux-gnu/", ENOENT, false);
	box.fs.grant_errno("/usr/bin/gnm", ENOENT, false);
	box.fs.grant_errno("/bin/gnm", ENOENT, false);
	box.fs.grant_native("/usr/bin/nm");
	box.fs.grant_errno("/usr/bin/gstrip", ENOENT, false);
	box.fs.grant_errno("/bin/gstrip", ENOENT, false);
	box.fs.grant_native("/usr/bin/strip");
	box.fs.grant_tmp("/tmp/", 10);
	box.wait();
}
#endif
#endif

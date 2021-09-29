// BoredomFS - because I didn't have anything better to do than learn the FUSE API.
#ifdef __linux__
#define FUSE_USE_VERSION 26
#include "arlib.h"
#include <fuse.h>
#include <errno.h>
#include "shared.h"

#if !defined(__O_LARGEFILE) || __O_LARGEFILE==0
#undef __O_LARGEFILE
#define __O_LARGEFILE 0x8000 // I don't know why the standard headers define this to zero
#endif
#ifndef FMODE_EXEC // not part of the Linux userspace or FUSE headers, other than a comment in asm-generic/fcntl.h
#define FMODE_EXEC 0x20 // but probably still counts as a stable ABI
#endif

static receiver* recvp;
static bytearray request_rsp;
static bytearray request(bytesr by)
{
	recvp->send(by);
//puts("-> "+tostringhex(tmp)+tostringhex(by));
	runloop::global()->enter();
//puts("<- "+tostringhex(request_rsp));
	return std::move(request_rsp);
}

#define STAT_BYTES_SIZE (1+8+4+4+4+4)
static bool read_stat(struct stat * stbuf, bytestream& in)
{
	uint8_t type = in.u8();
	if (type == 0) return false;
	if (type == 1) stbuf->st_mode = S_IFDIR | 0777;
	if (type == 2) stbuf->st_mode = S_IFREG | 0666;
	if (type == 3) stbuf->st_mode = S_IFREG | 0777;
	stbuf->st_nlink = 1;
	stbuf->st_size = in.u64l();
	stbuf->st_atim.tv_sec = in.u32l();
	stbuf->st_atim.tv_nsec = in.u32l();
	stbuf->st_mtim.tv_sec = in.u32l();
	stbuf->st_mtim.tv_nsec = in.u32l();
	stbuf->st_ctim = stbuf->st_mtim;
	stbuf->st_blksize = 512;
	stbuf->st_blocks = (stbuf->st_size + stbuf->st_blksize - 1) / stbuf->st_blksize;
	return true;
}

struct statcache {
	time_t expiry = 0;
	struct stat stbuf;
};
static map<string,statcache> g_cache;
struct dircache {
	time_t expiry = 0;
	array<string> contents;
};
static map<string,dircache> g_dir_cache;
static time_t g_cache_revalidate = 0;

static void cache_add(const char * path, struct stat * stbuf)
{
	time_t now = time(NULL);
	statcache& c = g_cache.get_create(path);
	c.stbuf = *stbuf;
	c.expiry = now+5;
	g_cache_revalidate = now+5;
}
static bool cache_get(const char * path, struct stat * stbuf)
{
	time_t now = time(NULL);
	if (now > g_cache_revalidate)
	{
		g_cache.reset();
		g_dir_cache.reset();
	}
	statcache* c = g_cache.get_or_null(path);
	if (c && c->expiry >= now)
	{
		*stbuf = c->stbuf;
		return true;
	}
	return false;
}

static array<string>& cache_dir_add(const char * path)
{
	time_t now = time(NULL);
	dircache& c = g_dir_cache.get_create(path);
	c.contents.reset();
	c.expiry = now+5;
	g_cache_revalidate = now+5;
	return c.contents;
}
static arrayview<string> cache_dir_get(const char * path)
{
	time_t now = time(NULL);
	if (now > g_cache_revalidate)
	{
		g_cache.reset();
		g_dir_cache.reset();
	}
	dircache* c = g_dir_cache.get_or_null(path);
	if (c && c->expiry >= now) return c->contents;
	return {};
}


struct my_file {
	bool is_exec;
	
	string exec_contents;
	
	
	int fd;
	
	// write cache
	off_t wrc_offset;
	uint32_t wrc_size = 0;
	bool wrc_ok = true;
	uint8_t wrc_buf[131072];
	
	void flush()
	{
		if (!wrc_size) return;
//printf("write %u to %zu\n", wrc_size, wrc_offset);
		
		bytestreamw req;
		req.u32l(REQ_WRITE);
		req.u32l(fd);
		req.u64l(wrc_offset);
		req.bytes(bytesr(wrc_buf, wrc_size));
		
		bytearray rsp_buf = request(req.finish());
		if (!rsp_buf) wrc_ok = false;
		if (readu_le32(rsp_buf.ptr()) != wrc_size) wrc_ok = false;
		wrc_size = 0;
	}
	
	void write(const char * buf, size_t size, off_t offset)
	{
		if (size > sizeof(this->wrc_buf)) abort();
		
		if (offset != this->wrc_offset+this->wrc_size || size+this->wrc_size > sizeof(this->wrc_buf))
			flush();
		if (this->wrc_size == 0)
			wrc_offset = offset;
		
		memcpy(this->wrc_buf+this->wrc_size, buf, size);
		this->wrc_size += size;
		
		if (this->wrc_size == sizeof(this->wrc_buf)) flush();
	}
};


int main(int argc, char** argv)
{
	if (sodium_init() < 0) abort();
	
	// remote exec is implemented as file content
	//  #!/home/alcaro/Desktop/bored/bored --remote-exec 192.168.0.1 /home/alcaro/Desktop/bored/mnt/ /Windows/System32/cmd.exe
	// argv[0] is /home/alcaro/Desktop/bored/bored
	// argv[1] is --remote-exec 192.168.0.1 /home/alcaro/Desktop/bored/mnt/ /Windows/System32/cmd.exe
	//  (identifier, host address, mount point, mount-relative executable)
	// argv[2] is the .exe, possibly relative to caller process' cwd
	// argv[3]+ is the actual arguments
	array<string> remote_param;
	bool is_remote_exec = false;
	if (argv[1])
	{
		remote_param = cstring(argv[1]).split<3>(" ");
		is_remote_exec = (remote_param[0] == "--remote-exec");
	}
	
	static string mountpoint;
	if (is_remote_exec) mountpoint = remote_param[2];
	else if (argc == 1) mountpoint = file::realpath("mnt");
	else mountpoint = file::realpath(argv[1]);
	if (!mountpoint.endswith("/")) mountpoint += "/";
	
	static string address;
	
	receiver recv;
	recvp = &recv;
	if (is_remote_exec)
	{
		address = remote_param[1];
		recv.init(socket::create(address, 3339, runloop::global()),
		              [](bytearray by){ request_rsp = std::move(by); runloop::global()->exit(); },
		              [](){ puts("connection failed"); exit(1); });
		
		bytestreamw req;
		req.u32l(REQ_EXEC);
		
		if (file::cwd().startswith(mountpoint))
			req.strnul(file::cwd().substr(mountpoint.length()-1, ~0));
		else
			req.strnul("/");
		req.strnul(remote_param[3]);
		for (int i=3;i<argc;i++) req.strnul(argv[i]);
		
		bytearray rsp_buf = request(req.finish());
		recv.callback([&](bytearray by){
			if (by.size() < 4) return;
			uint32_t type = readu_le32(by.ptr());
			if (type == REQ_EXEC_STDOUT)
				(void)! write(1, by.ptr()+4, by.size()-4);
			if (type == REQ_EXEC_EXIT && by.size() == 8)
				exit(readu_le32(by.ptr()+4));
		}, [](){ puts("sock error"); exit(1); });
		runloop::global()->set_fd(0, [](uintptr_t){
			uint8_t buf[1024];
			int n = read(0, buf+sizeof(uint32_t), sizeof(buf)-sizeof(uint32_t));
			if (n <= 0)
			{
				uint8_t msg[sizeof(uint32_t)];
				writeu_le32(msg, REQ_EXEC_STDIN_CLOSE);
				recvp->send(msg);
				runloop::global()->set_fd(0, nullptr);
				return;
			}
			writeu_le32(buf, REQ_EXEC_STDIN);
			recvp->send(bytesr(buf, sizeof(uint32_t)+n));
		});
		runloop::global()->enter();
		exit(1);
	}
	
	{
		// process:
		// - try to connect to every address
		// - on first success: start state::t, 100ms
		//    on expiry: break runloop
		//     this leads to caller deleting all existing receivers
		// - on any success including first:
		//    if current one's idx < state::chosen_idx:
		//     update chosen_idx
		//     overwrite out with current one's receiver
		// - if all fail: terminate process
		
		struct state;
		struct target {
			state* st;
			size_t idx;
			cstring address;
			receiver recv;
			
			void connect_done(bool success);
		};
		
		struct state {
			size_t n_total;
			size_t n_finished;
			size_t chosen_idx;
			DECL_G_TIMER(timeout, state);
			array<target> hosts;
			
			void connect_done(target& t, bool success)
			{
				if (success && t.idx < chosen_idx)
				{
					if (chosen_idx == n_total)
						timeout.set_once(100, [](){ runloop::global()->exit(); });
					chosen_idx = t.idx;
				}
				
				n_finished++;
				if (n_finished == n_total && chosen_idx == n_total)
					runloop::global()->exit();
			}
		};
		
		string tmp = file::readallt("hosts.txt");
		state st;
		for (cstring addr : tmp.csplit("\n"))
		{
			if (!addr || addr[0] == '#') continue;
			target& t = st.hosts.append();
			t.st = &st;
			t.idx = st.hosts.size()-1;
			t.address = addr;
		}
		if (!st.hosts)
		{
			puts("error: no host address configured");
			puts("put the IP or domain name of your desired target in hosts.txt");
			puts("if your target is on DHCP or otherwise has a variable address, list all possible ones; each will be tried in order");
			exit(1);
		}
		st.n_total = st.hosts.size();
		st.n_finished = 0;
		st.chosen_idx = st.hosts.size();
		
		for (target& t : st.hosts)
		{
			t.recv.init(socket::create(t.address, 3339, runloop::global()),
				[&t](bytearray by) { t.st->connect_done(t, true); },
				[&t]() { t.st->connect_done(t, false); });
		}
		
		runloop::global()->enter();
		if (st.chosen_idx == st.n_total)
		{
			puts("connections failed");
			exit(1);
		}
		
		recv.consume(st.hosts[st.chosen_idx].recv);
		recv.callback([](bytearray by){ request_rsp = std::move(by); runloop::global()->exit(); },
		              [](){ puts("failed"); exit(1); });
		address = st.hosts[st.chosen_idx].address;
		st.hosts.reset();
	}
	
	auto perftest = [](size_t sendamt, size_t recvamt, size_t count){
		bytestreamw a;
		a.u32l(REQ_PING);
		a.u32l(recvamt);
		for (size_t i=0;i<sendamt;i++) a.u8(0);
		bytearray b = a.finish();
		timer t;
		for (size_t i=0;i<count;i++)
		{
			printf("%zu/%zu\r", i, count);
			fflush(stdout);
			request(b);
		}
		size_t ms = t.ms();
		if (sendamt) printf("sent %zu*%zu=%zu bytes", sendamt, count, sendamt*count);
		if (sendamt && recvamt) printf(", ");
		if (recvamt) printf("got %zu*%zu=%zu bytes", recvamt, count, recvamt*count);
		printf(" in %zums, %f KB/s\n", ms, (double)(sendamt+recvamt)*count*1000/ms/1024);
	};
	
	//perftest(512, 0, 512);
	//perftest(4096, 0, 512);
	//perftest(65536, 0, 64);
	//perftest(524288, 0, 16);
	//perftest(16777000, 0, 1);
	//perftest(0, 512, 512);
	//perftest(0, 4096, 512);
	//perftest(0, 65536, 64);
	//perftest(0, 524288, 16);
	//perftest(0, 16777000, 1);
	//exit(0);
	
	struct fuse_operations f_ops = {};
	f_ops.getattr = [](const char * path, struct stat * stbuf) -> int
	{
		if (cache_get(path, stbuf)) return 0;
		
		bytestreamw req;
		req.u32l(REQ_STAT);
		req.strnul(path);
		bytearray rsp_buf = request(req.finish());
		if (rsp_buf.size() != STAT_BYTES_SIZE) return -EINVAL;
		
		bytestream rsp(rsp_buf);
		if (!read_stat(stbuf, rsp)) return -ENOENT;
		cache_add(path, stbuf);
		return 0;
	};
	f_ops.readdir = [](const char * path, void * buf, fuse_fill_dir_t filler, off_t offset,
	                   struct fuse_file_info * fi) -> int
	{
		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);
		
		arrayview<string> cached = cache_dir_get(path);
		if (cached)
		{
			for (const string& child : cached)
				filler(buf, child, NULL, 0);
			return 0;
		}
		
		bytestreamw req;
		req.u32l(REQ_READDIR);
		req.strnul(path);
		bytearray rsp_buf = request(req.finish());
		
		if (!rsp_buf) return -ENOENT;
		if (rsp_buf[rsp_buf.size()-1] != '\0') return -EINVAL;
		
		array<string>& dcache = cache_dir_add(path);
		
		struct stat stbuf = {};
		bytestream rsp(rsp_buf);
		rsp.u8();
		const char * path_base = (!strcmp(path, "/") ? "" : path);
		while (rsp.remaining())
		{
			if (rsp.remaining() < STAT_BYTES_SIZE+1) return -EINVAL;
			read_stat(&stbuf, rsp);
			const char * child = rsp.strnul_ptr();
			if (strchr(child, '/')) return -EINVAL;
			filler(buf, child, &stbuf, 0);
			cache_add((cstring)path_base+"/"+child, &stbuf);
			dcache.append(child);
		}
		
		return 0;
	};
	static auto shared_open = [](const char * path, bool create, struct fuse_file_info * fi) -> int
	{
		int linux_flags = fi->flags;
		linux_flags &= ~__O_LARGEFILE;
		linux_flags &= ~O_NOFOLLOW; // symlinks are near-extinct on windows anyways
		linux_flags &= ~O_NONBLOCK; // I don't know why this is passed to fuse, doesn't make any sense over here
//printf("cr=%d fl=%o flok=%o\n",create,linux_flags,O_ACCMODE | O_APPEND | O_CREAT | O_EXCL | O_NOATIME | O_TRUNC);
		if (linux_flags & ~(O_ACCMODE | O_APPEND | O_CREAT | O_EXCL | O_NOATIME | O_TRUNC))
			return -EINVAL;
		uint32_t my_flags = 0;
		if ((linux_flags & O_ACCMODE) != O_RDONLY)
		{
			if (linux_flags & O_EXCL) my_flags |= 2;
			else if (create) my_flags |= 3;
			else my_flags |= 1;
		}
		if (linux_flags & O_TRUNC) my_flags |= 4;
		if (linux_flags & O_APPEND) my_flags |= 8;
		if (linux_flags & O_NOATIME) my_flags |= 16;
		
		bytestreamw req;
		req.u32l(REQ_OPEN);
		req.strnul(path);
		req.u32l(my_flags);
		
		bytearray rsp_buf = request(req.finish());
		
		if (!rsp_buf) return -ENOENT;
		if (rsp_buf.size() != 4) return -EINVAL;
		
		my_file* f = new my_file;
		f->is_exec = false;
		f->fd = readu_le32(rsp_buf.ptr());
		fi->fh = (uintptr_t)f;
		return 0;
	};
	f_ops.open = [](const char * path, struct fuse_file_info * fi) -> int
	{
		if (fi->flags & FMODE_EXEC)
		{
			my_file* f = new my_file;
			f->is_exec = true;
			f->exec_contents = (cstring)"#!"+file::exepath()+"bored --remote-exec "+address+" "+mountpoint+" "+path+"\n";
			fi->fh = (uintptr_t)f;
			return 0;
		}
		
		return shared_open(path, false, fi);
	};
	f_ops.create = [](const char * path, mode_t mode, struct fuse_file_info * fi) -> int
	{
		g_cache_revalidate = 0;
		return shared_open(path, true, fi);
	};
	f_ops.read = [](const char * path, char * buf, size_t size, off_t offset, struct fuse_file_info * fi) -> int
	{
		my_file* f = (my_file*)fi->fh;
		
		if (f->is_exec)
		{
			size_t actual = min(size, f->exec_contents.length()-offset);
			memcpy(buf, f->exec_contents.bytes().ptr()+offset, actual);
			return actual;
		}
		
		f->flush();
		
		bytestreamw req;
		req.u32l(REQ_READ);
		req.u32l(f->fd);
		req.u64l(offset);
		req.u32l(size);
		
		bytearray rsp_buf = request(req.finish());
		if (rsp_buf.size() > size) return -EINVAL;
		memcpy(buf, rsp_buf.ptr(), rsp_buf.size());
		return rsp_buf.size();
	};
	f_ops.write = [](const char * path, const char * buf, size_t size, off_t offset, struct fuse_file_info * fi) -> int
	{
		my_file* f = (my_file*)fi->fh;
		if (f->is_exec) return -EIO;
		
		f->write(buf, size, offset);
		if (!f->wrc_ok)
		{
			f->wrc_ok = true;
			return -EIO;
		}
		return size;
	};
	f_ops.release = [](const char * path, struct fuse_file_info * fi) -> int
	{
		my_file* f = (my_file*)fi->fh;
		if (f->is_exec)
		{
			delete f;
			return 0;
		}
		
		f->flush();
		bool wrc_ok = f->wrc_ok;
		
		bytestreamw req;
		req.u32l(REQ_CLOSE);
		req.u32l(f->fd);
		request(req.finish()); // ignore return value
		
		delete f;
		return wrc_ok ? 0 : -EIO;
	};
	f_ops.rename = [](const char * oldpath, const char * newpath) -> int
	{
		g_cache_revalidate = 0;
		
		bytestreamw req;
		req.u32l(REQ_RENAME);
		req.strnul(oldpath);
		req.strnul(newpath);
		
		bytearray rsp_buf = request(req.finish());
		if (rsp_buf) return 0;
		else return -ENOENT;
	};
	f_ops.unlink = [](const char * path) -> int
	{
		g_cache_revalidate = 0;
		
		bytestreamw req;
		req.u32l(REQ_DELETE);
		req.strnul(path);
		
		bytearray rsp_buf = request(req.finish());
		if (rsp_buf) return 0;
		else return -ENOENT;
	};
	f_ops.mkdir = [](const char * path, mode_t mode) -> int
	{
		g_cache_revalidate = 0;
		
		bytestreamw req;
		req.u32l(REQ_MKDIR);
		req.strnul(path);
		
		bytearray rsp_buf = request(req.finish());
		if (rsp_buf) return 0;
		else return -ENOENT;
	};
	f_ops.rmdir = [](const char * path) -> int
	{
		g_cache_revalidate = 0;
		
		bytestreamw req;
		req.u32l(REQ_RMDIR);
		req.strnul(path);
		
		bytearray rsp_buf = request(req.finish());
		if (rsp_buf) return 0;
		else return -ENOENT;
	};
	
	array<const char *> my_argv;
	my_argv.append(argv[0]);
	my_argv.append("-d"); // debug
	my_argv.append("-f"); // foreground
	my_argv.append("-s"); // single thread
	my_argv.append("-o"); my_argv.append("auto_unmount"); // auto unmount on process termination
	my_argv.append("-o"); my_argv.append("hard_remove"); // don't pretend to remove files (documented not recommended, I'll find out why)
	my_argv.append("-o"); my_argv.append("atomic_o_trunc"); // pass O_TRUNC to open(), don't call truncate() first
	my_argv.append("-o"); my_argv.append("big_writes"); // write more than 4KB at the time
	if (argc == 1) my_argv.append(mountpoint);
	for (int i=1;i<argc;i++) my_argv.append(argv[i]);
	my_argv.append(NULL);
	fuse_main(my_argv.size()-1, (char**)my_argv.ptr(), &f_ops, NULL);
	return 0;
}
#endif

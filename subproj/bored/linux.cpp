// BoredomFS - because I didn't have anything better to do than learn the FUSE API.
#ifdef __linux__
#define FUSE_USE_VERSION 26
#include "arlib.h"
#include <fuse.h>
#include <errno.h>
#include "shared.h"
#include <sys/ioctl.h>

#if !defined(__O_LARGEFILE) || __O_LARGEFILE==0
#undef __O_LARGEFILE
#define __O_LARGEFILE 0x8000 // I don't know why the standard headers define this to zero
#endif
#ifndef O_EXEC // not part of the Linux userspace or FUSE headers, other than a comment in asm-generic/fcntl.h (under the name FMODE_EXEC)
#define O_EXEC 0x20 // but documented somewhat in <https://libfuse.github.io/doxygen/structfuse__operations.html>
#endif

static receiver* recvp;
static bytearray request_rsp;
static bytearray request(bytesr by)
{
	recvp->send(by);
//puts("-> "+tostringhex(tmp)+tostringhex(by));
	runloop2::run([]() -> async<void> {
		request_rsp = co_await recvp->recv();
	}());
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
		
		bytestreamw_dyn req;
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


#define EXEC_PARAMS_SIZE 1024
#define IOCTL_EXEC_PARAMS _IOC(_IOC_READ,0xB0,0,EXEC_PARAMS_SIZE)
static_assert(EXEC_PARAMS_SIZE <= _IOC_SIZEMASK);

int main(int argc, char** argv)
{
	if (sodium_init() < 0) abort();
	static string exec_contents = (cstring)"#!"+file::exepath()+" --remote-exec";
	
	// remote exec is implemented as file content #!/home/walrus/Desktop/bored/bored --remote-exec
	// (FUSE docs <https://libfuse.github.io/doxygen/structfuse__operations.html> say O_EXEC is for permission checks only, but who cares)
	// argv[0] is /home/walrus/Desktop/bored/bored
	// argv[1] is --remote-exec
	// argv[2] is the .exe, possibly relative to caller process' cwd
	// argv[3]+ is the actual arguments
	// argv[2] will be opened and a BoredomFS-only ioctl issued, returning linebreak-separated
	//  IP address, port, key, mount path, mount-relative exe filename
	//  I could've put this stuff in the file content, but its size is limited to 128 and can't fit the key
	
	if (argv[1] && !strcmp(argv[1], "--remote-exec"))
	{
		char buf[EXEC_PARAMS_SIZE];
		int fd = open(argv[2], O_RDONLY);
		if (fd < 0 || ioctl(fd, IOCTL_EXEC_PARAMS, buf) < 0)
		{
			puts("bored: internal error");
			return 1;
		}
		close(fd);
		array<cstring> remote_param = cstring(buf).csplit("\n");
		
		if (!init_key(remote_param[2]))
		{
			puts("bored: bad key");
			return 1;
		}
		
		string mountpoint = remote_param[3];
		
		auto my_coro = [&]() -> async<void> {
			receiver recv;
			co_await recv.init(remote_param[0], try_fromstring<uint16_t>(remote_param[1]));
			if (!recv.alive())
			{
				puts("bored: connection failed");
				_exit(1);
			}
			
			bytestreamw_dyn req;
			req.u32l(REQ_EXEC);
			
			if (file::cwd().startswith(mountpoint))
				req.strnul(file::cwd().substr(mountpoint.length()-1, ~0));
			else
				req.strnul(file::dirname(remote_param[4]));
			req.strnul(remote_param[4]);
			for (int i=3;i<argc;i++) req.strnul(argv[i]);
			
			bytearray rsp_buf = co_await recv.request(req.finish());
			
			auto my_inner_coro = [&]() -> async<void> {
				while (true)
				{
					bytearray by = co_await recv.recv();
					if (!recv.alive() || by.size() < 4)
					{
						puts("bored: sock error");
						_exit(1);
					}
					uint32_t type = readu_le32(by.ptr());
					if (type == REQ_EXEC_STDOUT)
						(void)! write(1, by.ptr()+4, by.size()-4);
					if (type == REQ_EXEC_EXIT && by.size() == 8)
						_exit(readu_le32(by.ptr()+4));
				}
			};
			runloop2::detach(my_inner_coro());
			
			while (true)
			{
				co_await runloop2::await_read(STDIN_FILENO);
				uint8_t buf[1024];
				int n = read(0, buf+sizeof(uint32_t), sizeof(buf)-sizeof(uint32_t));
				if (n <= 0)
				{
					uint8_t msg[sizeof(uint32_t)];
					writeu_le32(msg, REQ_EXEC_STDIN_CLOSE);
					recv.send(msg);
				}
				writeu_le32(buf, REQ_EXEC_STDIN);
				recv.send(bytesr(buf, sizeof(uint32_t)+n));
			}
		};
		
		runloop2::run(my_coro());
		exit(1);
	}
	
	string host;
	static uint16_t port = 3339;
	array<string> fuse_args;
	string key_text;
	string remote_exec;
	
	argparse args;
	args.add('h', "host", &host);
	args.add('p', "port", &port);
	args.add('k', "key", &key_text);
	args.add("", &fuse_args);
	args.parse(argv);
	
	if (!init_key(key_text))
	{
		puts("bad key");
		return 1;
	}
	
	if (!fuse_args) fuse_args.append("mnt");
	
	static string mountpoint = file::realpath(fuse_args[fuse_args.size()-1]);
	if (!mountpoint.endswith("/")) mountpoint += "/";
	
	static string address;
	
	receiver recv;
	recvp = &recv;
	
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
		
		static size_t n_completed = 0;
		static size_t n_total = 0;
		static size_t idx_best_success = SIZE_MAX;
		
		auto try_connect = [](size_t idx, cstring addr, uint16_t port) -> async<void> {
			receiver recv;
			co_await recv.init(addr, port);
			n_completed++;
			if (recv.alive() && idx < idx_best_success)
			{
puts("connected "+addr);
				*recvp = std::move(recv);
				idx_best_success = idx;
				address = addr;
			}
			co_await runloop2::in_ms(100);
			n_completed = n_total;
		};
		
		co_holder children;
		
		string tmp;
		if (host) tmp = host;
		else tmp = file::readallt("hosts.txt");
		for (cstring addr : tmp.csplit("\n"))
		{
			if (!addr || addr[0] == '#') continue;
			children.add(try_connect(n_total++, addr, port));
		}
		if (!n_total)
		{
			puts("error: no host address configured");
			puts("put the IP or domain name of your desired target in hosts.txt");
			puts("if your target is on DHCP or otherwise has a variable address, list all possible ones; each will be tried in order");
			exit(1);
		}
		
		while (n_completed < n_total)
			runloop2::step();
	}
	
	auto perftest = [](size_t sendamt, size_t recvamt, size_t count){
		bytestreamw_dyn a;
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
	puts("connected");
	
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
		
		bytestreamw_dyn req;
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
		
		bytestreamw_dyn req;
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
		
		bytestreamw_dyn req;
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
		if (fi->flags & O_EXEC)
		{
			my_file* f = new my_file;
			f->is_exec = true;
			fi->fh = (uintptr_t)f;
			// disable page cache for exec requests
			// direct_io reduces performance and disables mmap, but that's fine for a file that's just a shebang
			// fi->keep_cache also exists, but it's off by default; I'm not sure how exactly kernel decides what to cache
			fi->direct_io = true;
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
		my_file* f = (my_file*)(uintptr_t)fi->fh;
		
		if (f->is_exec)
		{
			size_t actual = min(size, exec_contents.length()-offset);
			memcpy(buf, exec_contents.bytes().ptr()+offset, actual);
			return actual;
		}
		
		f->flush();
		
		bytestreamw_dyn req;
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
		
		bytestreamw_dyn req;
		req.u32l(REQ_CLOSE);
		req.u32l(f->fd);
		request(req.finish()); // ignore return value
		
		delete f;
		return wrc_ok ? 0 : -EIO;
	};
	f_ops.rename = [](const char * oldpath, const char * newpath) -> int
	{
		g_cache_revalidate = 0;
		
		bytestreamw_dyn req;
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
		
		bytestreamw_dyn req;
		req.u32l(REQ_DELETE);
		req.strnul(path);
		
		bytearray rsp_buf = request(req.finish());
		if (rsp_buf) return 0;
		else return -ENOENT;
	};
	f_ops.mkdir = [](const char * path, mode_t mode) -> int
	{
		g_cache_revalidate = 0;
		
		bytestreamw_dyn req;
		req.u32l(REQ_MKDIR);
		req.strnul(path);
		
		bytearray rsp_buf = request(req.finish());
		if (rsp_buf) return 0;
		else return -ENOENT;
	};
	f_ops.rmdir = [](const char * path) -> int
	{
		g_cache_revalidate = 0;
		
		bytestreamw_dyn req;
		req.u32l(REQ_RMDIR);
		req.strnul(path);
		
		bytearray rsp_buf = request(req.finish());
		if (rsp_buf) return 0;
		else return -ENOENT;
	};
	f_ops.statfs = [](const char * path, struct statvfs * buf) -> int
	{
		bytestreamw_dyn req;
		req.u32l(REQ_STATFS);
		bytearray rsp_buf = request(req.finish());
		uint64_t total = readu_le64(rsp_buf.ptr()+0);
		uint64_t free = readu_le64(rsp_buf.ptr()+8);
		
		buf->f_bsize = 512;
		buf->f_frsize = 512;
		buf->f_blocks = total/512;
		buf->f_bfree = free/512;
		buf->f_bavail = free/512;
		buf->f_files = 0;
		buf->f_ffree = 0;
		buf->f_favail = 0;
#ifndef NTFS_SB_MAGIC
#define NTFS_SB_MAGIC 0x5346544e
#endif
		buf->f_fsid = NTFS_SB_MAGIC;
		buf->f_flag = ST_NOATIME|ST_NODEV|ST_NOSUID;
		buf->f_namemax = 0;
		return 0;
	};
	f_ops.ioctl = [](const char * path, int cmd, void* arg,
	                 struct fuse_file_info * fi, unsigned int flags, void* data) -> int
	{
		if ((unsigned int)cmd == IOCTL_EXEC_PARAMS)
		{
			string params = address+"\n"+tostring(port)+"\n"+tostringhex(the_key)+"\n"+mountpoint+"\n"+path;
			if (params.length() >= EXEC_PARAMS_SIZE) return -ENOBUFS;
			
			memset(data, 0, EXEC_PARAMS_SIZE);
			memcpy(data, params.bytes().ptr(), params.length());
			return 0;
		}
		return -ENOSYS;
	};
	
	array<const char *> my_argv;
	my_argv.append(argv[0]);
	my_argv.append("-d"); // debug
	my_argv.append("-f"); // foreground
	// the vast majority of these belong in something like f_ops.flags, not the shell invocation
	my_argv.append("-s"); // single thread
	my_argv.append("-o"); my_argv.append("auto_unmount"); // auto unmount on process termination
	my_argv.append("-o"); my_argv.append("hard_remove"); // don't pretend to remove files (windows doesn't support removed files anyways)
	my_argv.append("-o"); my_argv.append("atomic_o_trunc"); // pass O_TRUNC to open(), don't call truncate() first
	my_argv.append("-o"); my_argv.append("big_writes"); // write more than 4KB at the time
	for (string& arg : fuse_args) my_argv.append(arg);
	my_argv.append(NULL);
	fuse_main(my_argv.size()-1, (char**)my_argv.ptr(), &f_ops, NULL);
	return 0;
}
#endif

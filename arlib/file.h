#pragma once
#include "global.h"
#include "string.h"
#include "array.h"

class file : nocopy {
public:
	class impl : nocopy {
	public:
		virtual size_t size() = 0;
		virtual bool resize(size_t newsize) = 0;
		
		virtual size_t read(arrayvieww<byte> target, size_t start) = 0;
		virtual bool write(arrayview<byte> data, size_t start = 0) = 0;
		virtual bool replace(arrayview<byte> data) { return resize(data.size()) && write(data); }
		
		virtual arrayview<byte> mmap(size_t start, size_t len) = 0;
		virtual void unmap(arrayview<byte> data) = 0;
		virtual arrayvieww<byte> mmapw(size_t start, size_t len) = 0;
		virtual void unmapw(arrayvieww<byte> data) = 0;
		
		virtual ~impl() {}
	};
	
	class implrd : public impl {
	public:
		virtual size_t size() = 0;
		bool resize(size_t newsize) { return false; }
		
		virtual size_t read(arrayvieww<byte> target, size_t start) = 0;
		bool write(arrayview<byte> data, size_t start = 0) { return false; }
		bool replace(arrayview<byte> data) { return false; }
		
		virtual arrayview<byte> mmap(size_t start, size_t len) = 0;
		virtual void unmap(arrayview<byte> data) = 0;
		arrayvieww<byte> mmapw(size_t start, size_t len) { return NULL; }
		void unmapw(arrayvieww<byte> data) {}
	};
private:
	impl* core;
	file(impl* core) : core(core) {}
	
public:
	enum mode {
		m_read,
		m_write,          // If the file exists, opens it. If it doesn't, creates a new file.
		m_wr_existing,    // Fails if the file doesn't exist.
		m_replace,        // If the file exists, it's either deleted and recreated, or truncated.
		m_create_excl,    // Fails if the file does exist.
	};
	
	file() : core(NULL) {}
	file(file&& f) { core=f.core; f.core=NULL; }
	file& operator=(file&& f) { delete core; core=f.core; f.core=NULL; return *this; }
	file(cstring filename, mode m = m_read) : core(NULL) { open(filename, m); }
	
	bool open(cstring filename, mode m = m_read)
	{
		delete core;
		core = open_impl(filename, m);
		return core;
	}
	void close()
	{
		delete core;
		core = NULL;
	}
	static file wrap(impl* core) { return file(core); }
	
private:
	//This one will create the file from the filesystem.
	//create() can simply return create_fs(filename), or can additionally support stuff like gvfs.
	static impl* open_impl_fs(cstring filename, mode m);
	//A path refers to a directory if it ends with a slash, and file otherwise. Directories may not be open()ed.
	static impl* open_impl(cstring filename, mode m);
public:
	
	operator bool() const { return core; }
	
	//Reading outside the file will return partial results.
	size_t size() const { return core->size(); }
	size_t read(arrayvieww<byte> target, size_t start) const { return core->read(target, start); }
	array<byte> read() const
	{
		array<byte> ret;
		ret.reserve_noinit(this->size());
		size_t actual = this->read(ret, 0);
		ret.resize(actual);
		return ret;
	}
	static array<byte> read(cstring path)
	{
		file f(path);
		if (f) return f.read();
		else return NULL;
	}
	
	//May only be used if there are no mappings alive, not even read-only.
	bool resize(size_t newsize) { return core->resize(newsize); }
	//Writes outside the file will extend it. If the write starts after the current size, it's zero extended. Includes mmapw.
	bool write(arrayview<byte> data, size_t start = 0) { return core->write(data, start); }
	bool replace(arrayview<byte> data) { return core->replace(data); }
	bool replace(cstring data) { return replace(data.bytes()); }
	bool write(cstring data) { return write(data.bytes()); }
	
	//Mappings must be deallocated before deleting the file object.
	//If the underlying file is changed, it's undefined whether the mappings update. To force an update, delete and recreate the mapping.
	//Mapping outside the file is undefined behavior.
	arrayview<byte> mmap(size_t start, size_t len) const { return core->mmap(start, len); }
	arrayview<byte> mmap() const { return this->mmap(0, this->size()); }
	void unmap(arrayview<byte> data) const { return core->unmap(data); }
	
	arrayvieww<byte> mmapw(size_t start, size_t len) { return core->mmapw(start, len); }
	arrayvieww<byte> mmapw() { return this->mmapw(0, this->size()); }
	void unmapw(arrayvieww<byte> data) { return core->unmapw(data); }
	
	~file() { delete core; }
	
	static file mem(arrayview<byte> data)
	{
		return file(new file::memimpl(data));
	}
	//the array may not be modified while the file object exists, other than via the file object itself
	static file mem(array<byte>& data)
	{
		return file(new file::memimpl(&data));
	}
private:
	class memimpl : public file::implrd {
	public:
		arrayview<byte> datard;
		array<byte>* datawr; // this object does not own the array
		
		memimpl(arrayview<byte> data) : datard(data), datawr(NULL) {}
		memimpl(array<byte>* data) : datard(*data), datawr(data) {}
		
		size_t size() { return datard.size(); }
		bool resize(size_t newsize)
		{
			if (!datawr) return false;
			datawr->resize(newsize);
			datard=*datawr;
			return true;
		}
		
		size_t read(arrayvieww<byte> target, size_t start)
		{
			size_t nbyte = min(target.size(), datard.size()-start);
			memcpy(target.ptr(), datard.slice(start, nbyte).ptr(), nbyte);
			return nbyte;
		}
		virtual bool write(arrayview<byte> newdata, size_t start = 0)
		{
			if (!datawr) return false;
			size_t nbyte = newdata.size();
			datawr->reserve_noinit(start+nbyte);
			memcpy(datawr->slice(start, nbyte).ptr(), newdata.ptr(), nbyte);
			datard=*datawr;
			return true;
		}
		virtual bool replace(arrayview<byte> newdata)
		{
			if (!datawr) return false;
			*datawr = newdata;
			datard = *datawr;
			return true;
		}
		
		arrayview<byte>   mmap(size_t start, size_t len) { return datard.slice(start, len); }
		arrayvieww<byte> mmapw(size_t start, size_t len) { if (!datawr) return NULL; return datawr->slice(start, len); }
		void  unmap(arrayview<byte>  data) {}
		void unmapw(arrayvieww<byte> data) {}
	};
public:
	
	//Returns all items in the given directory path, as absolute paths.
	static array<string> listdir(cstring path);
	static bool unlink(cstring filename); // Returns whether the file is now gone. If the file didn't exist, returns true.
	//If the input path is a directory, the basename is blank.
	static string dirname(cstring path);
	static string basename(cstring path);
private:
	static bool unlink_fs(cstring filename);
};


class autommap : public arrayview<byte> {
	const file& f;
public:
	autommap(const file& f, arrayview<byte> b) : arrayview(b), f(f) {}
	autommap(const file& f, size_t start, size_t end) : arrayview(f.mmap(start, end)), f(f) {}
	autommap(const file& f) : arrayview(f.mmap()), f(f) {}
	~autommap() { f.unmap(*this); }
};

class autommapw : public arrayvieww<byte> {
	file& f;
public:
	autommapw(file& f, arrayvieww<byte> b) : arrayvieww(b), f(f) {}
	autommapw(file& f, size_t start, size_t end) : arrayvieww(f.mmapw(start, end)), f(f) {}
	autommapw(file& f) : arrayvieww(f.mmapw()), f(f) {}
	~autommapw() { f.unmapw(*this); }
};


#ifdef __unix__
//Note that these may return false positives. Use nonblocking operations.
class fd_mon : nocopy {
#ifdef __linux__
	int epoll_fd;
#endif
	
public:
	fd_mon();
	void monitor(int fd, void* key, bool read = true, bool write = false);
	void remove(int fd) { monitor(fd, NULL, false, false); }
	//Returns whatever key is associated with this fd, or NULL for timeout.
	//To detect timeout vs key==NULL, check can_read/can_write, they're both false on timeout.
	void* select(int timeout_ms = -1) { bool x; return select(&x, NULL, timeout_ms); }
	void* select(bool* can_read, bool* can_write, int timeout_ms = -1);
	~fd_mon();
};

//Returns the array index.
int fd_monitor_oneshot(arrayview<int> fds, bool* can_read, bool* can_write, int timeout_ms = -1);
inline int fd_monitor_oneshot(arrayview<int> fds, int timeout_ms = -1) { bool x; return fd_monitor_oneshot(fds, &x, NULL, timeout_ms); }

#ifdef ARLIB_THREAD
//Monitors fds from a separate thread.
//To unregister a callback, fd_mon_thread(fd, NULL, NULL). fd must be open at this point.
//After fd_mon_thread returns, the previous callback is guaranteed to have returned for the last time.
// (Exception: If fd_mon_thread is called from its own callbacks, it will be allowed to finish.)
//Callbacks are called on a foreign thread. Use locks as appropriate.
//Do not hold any locks needed by on_read or on_write while calling this.
//Argument is the same fd, to allow one object to monitor multiple fds.
void fd_mon_thread(int fd, function<void(int)> on_read, function<void(int)> on_write);
#endif
#endif

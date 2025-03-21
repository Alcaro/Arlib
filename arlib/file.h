#pragma once
#include "global.h"
#include "string.h"
#include "array.h"
#include "time.h"
#include "bytepipe.h"
#ifdef __unix__
#include <unistd.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <dirent.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

// TODO: this class needs to be rewritten
// commonly needed usecases, like file::readall, should not depend on unnecessary functionality, like seeking
// (/dev/stdin is unseekable, and gvfs http seek is unreliable)
//
// usecases:
// read all, read streaming, read random
// replace contents, replace/append streaming, rw random
// the above six but async
// mmap read, mmap rw
//  no need for mmap partial; it'd make sense on 32bit, but 32bit is no longer useful
// read all and flock() - also allows replacing contents
//  actually, this should be the one with replace contents
//
// read all should, as now, be a single function, but implemented in the backend
//  since typical usecase is passing return value to e.g. JSON::parse, it should return normal array<uint8_t> or string
// mmap shouldn't be in the file object either, but take a filename and return an arrayview(w)<uint8_t> child with a dtor
//  mmap resizable 
// read-and-lock should also be a function, also returning an arrayview<uint8_t> child with a dtor
//  this one should also have a 'replace contents' member, initially writing to filename+".swptmp" (remember to fsync)
//   the standard recommendation is using unique names; the advantage is no risk that two programs use the temp name simultaneously,
//    but the drawback is a risk of leaving trash around that will never be cleaned
//    such overlapping writes are unsafe anyways - if two programs simultaneously update a file using unique names,
//     one will be reverted, after telling program it succeeded
//   ideally, it'd be open(O_TMPFILE)+linkat(O_REPLACE), but kernel doesn't allow that
//   alternatively ioctl(FISWAPRANGE) https://lwn.net/Articles/818977/, except it was never merged
//   for Windows, just go for ReplaceFile(), don't even try to not create temp files (fsync() is FlushFileBuffers())
// async can be ignored for now; it's rarely useful, and async without coroutines is painful
// the other four combinations belong in the file object; replace/append streaming is useful for logs
//  they should use seek/read/size as primitives, not pread
//
// may also want some kind of resilient file object, guaranteeing complete write or rollback
// unfortunately, guarantees about mmap (or even write) atomicity are rare or nonexistent
//  other than rename-overwrite, which requires a poorly documented fsync dance before any atomicity guarantees exist,
//   and does way more than it should
//
// http support should be deprecated, filesystem only

#ifdef _WIN32
struct iovec {
	void* iov_base;
	size_t iov_len;
};
typedef off64_t off_t;
static_assert(sizeof(off_t) == 8);
#endif

template<typename T> class async;

#ifdef __unix__
typedef int fd_raw_t;
#define NULL_FD (-1)
#endif
#ifdef _WIN32
typedef HANDLE fd_raw_t;
#define NULL_FD INVALID_HANDLE_VALUE
#endif
class fd_t {
	fd_raw_t val;
	
public:
	fd_t() : val(NULL_FD) {}
	fd_t(fd_raw_t raw) : val(raw) {}
	fd_t(const fd_t&) = delete;
	fd_t(fd_t&& other) { val = other.val; other.val = NULL_FD; }
	
	fd_t& operator=(const fd_t&) = delete;
	fd_t& operator=(fd_t&& other) { close(); val = other.val; other.val = NULL_FD; return *this; }
	fd_t& operator=(fd_raw_t raw) { close(); val = raw; return *this; }
	operator fd_raw_t() const { return val; }
	operator bool() const = delete; // ambiguous with operator int
	
	bool valid() const { return val != NULL_FD; }
	
	static fd_raw_t null() { return NULL_FD; }
	
	static void close(fd_raw_t val)
	{
		if (val == NULL_FD)
			return;
#ifdef __unix__
		::close(val);
#endif
#ifdef _WIN32
		::CloseHandle(val);
#endif
	}
	fd_raw_t release()
	{
		fd_raw_t ret = val;
		val = NULL_FD;
		return ret;
	}
	void close() { close(val); val = NULL_FD; }
	~fd_t() { close(); }
	
	static fd_t create_devnull();
};
#undef NULL_FD

class file2 : nocopy {
	fd_t fd;
	void reset() { fd.close(); }
	
public:
	enum mode {
		m_read,
		m_readwrite,      // If the file exists, opens it. If it doesn't, creates a new file.
		m_wr_existing,    // Fails if the file doesn't exist.
		m_replace,        // If the file exists, it's either deleted and recreated, or truncated.
		m_create_excl,    // Fails if the file does exist.
		
		m_exclusive = 8, // OR with any other mode. Tries to claim exclusive write access, or with m_read, simply deny all writers.
		                 // The request may be bypassable, or not honored by default, on some OSes.
		                 // However, if both processes use Arlib, the second one will be stopped.
	};
	
	class mmapw_t;
	class mmap_t : public arrayview<uint8_t>, nocopy {
		friend class file2;
		friend class mmapw_t;
		static void map(bytesr& by, file2& src, bool writable, bool small);
		static void unmap(bytesr& by, bool small);
		
		void unmap() { unmap(*this, true); }
		void move_from(bytesr& other) { arrayview::operator=(other); other = nullptr; }
	public:
		mmap_t() {}
		mmap_t(mmap_t&& other) { move_from(other); }
		mmap_t(mmapw_t&& other) { move_from(other); }
		mmap_t& operator=(nullptr_t) { unmap(); return *this; }
		mmap_t& operator=(bytesr) = delete;
		mmap_t& operator=(mmap_t&& other) { unmap(); move_from(other); return *this; }
		~mmap_t() { unmap(); }
	};
	class mmapw_t : public arrayvieww<uint8_t>, nocopy {
		friend class file2;
		void unmap() { mmap_t::unmap(*this, false); }
		void move_from(bytesr& other) { arrayview::operator=(other); other = nullptr; }
	public:
		mmapw_t() {}
		mmapw_t(const mmapw_t&) = delete;
		mmapw_t(mmapw_t&& other) { move_from(other); }
		mmapw_t& operator=(nullptr_t) { unmap(); return *this; }
		mmapw_t& operator=(bytesr) = delete;
		mmapw_t& operator=(mmapw_t&& other) { unmap(); move_from(other); return *this; }
		~mmapw_t() { unmap(); }
	};
	
	file2() {}
	file2(file2&& f) { fd = std::move(f.fd); }
	file2& operator=(file2&& f) { reset(); fd = std::move(f.fd); return *this; }
	file2(cstrnul filename, mode m = m_read) { open(filename, m); }
	~file2() { reset(); }
	bool open(cstrnul filename, mode m = m_read);
#ifdef __unix__
	bool openat(fd_raw_t dirfd, cstrnul filename, mode m = m_read);
#endif
	void close() { reset(); }
	operator bool() const { return fd.valid(); }
	
	void open_usurp(fd_t fd) { this->fd = std::move(fd); }
	fd_raw_t peek_handle() { return this->fd; }
	fd_t release_handle() { return std::move(this->fd); }
	void create_dup(fd_t fd); // consider open_usurp() and release_handle(), if possible; they're faster
	
#ifndef __unix__
	// All return number of bytes processed. 0 means both EOF and error, return can't be negative.
	// Max size is 2**31-1 bytes, for both reads and writes.
	size_t read(bytesw by);
	size_t pread(off_t pos, bytesw by); // The p variants will not affect the current write pointer.
	size_t write(bytesr by);
	size_t write(cstring str) { return write(str.bytes()); }
	size_t pwrite(off_t pos, bytesr by);
	size_t writev(arrayview<iovec> iov); // No readv, hard to know sizes before reading.
	size_t pwritev(off_t pos, arrayview<iovec> iov);
	// TODO: async read/write (io_uring or WriteFileEx), but as a separate class (windows needs FILE_FLAG_OVERLAPPED)
	
	size_t sector_size();
	
	off_t size(); // Will return zero for certain weird files, like /dev/urandom.
	bool resize(off_t newsize); // May or may not seek to the file's new size.
	void seek(off_t pos); // Seeking outside the file is not allowed.
	off_t tell();
	
	timestamp time();
	void set_time(timestamp t);
#else
	size_t read(bytesw by) { return max(::read(fd, by.ptr(), by.size()), 0); }
	size_t pread(off_t pos, bytesw by) { return max(::pread(fd, by.ptr(), by.size(), pos), 0); }
	size_t write(bytesr by) { return max(::write(fd, by.ptr(), by.size()), 0); }
	size_t write(cstring str) { return write(str.bytes()); }
	size_t pwrite(off_t pos, bytesr by) { return max(::pwrite(fd, by.ptr(), by.size(), pos), 0); }
	size_t writev(arrayview<iovec> iov) { return max(::writev(fd, iov.ptr(), iov.size()), 0); }
	size_t pwritev(off_t pos, arrayview<iovec> iov) { return max(::pwritev(fd, iov.ptr(), iov.size(), pos), 0); }
	
	size_t sector_size() { struct stat st; fstat(fd, &st); return st.st_blksize; }
	
	off_t size() { struct stat st; fstat(fd, &st); return st.st_size; }
	bool resize(off_t newsize) { return (ftruncate(fd, newsize) == 0); }
	//void seek(off_t pos);
	//off_t tell() { 
	
	timestamp time() { struct stat st; fstat(fd, &st); return timestamp::from_native(st.st_mtim); }
	void set_time(timestamp t) { struct timespec times[] = { { 0, UTIME_OMIT }, t.to_native() }; futimens(fd, times); }
#endif
	
	// mmap objects are not tied to the file object. You can close the file and keep using the mmap, unless you need the sync_map function.
	// It is undefined whether the mmap_t object updates if the underlying file does.
	mmap_t mmap() { return mmapw(false, true); }
	static mmap_t mmap(cstrnul filename)
	{
		file2 f(filename);
		if (!f)
			return {};
		return f.mmap();
	}
	// This one is guaranteed to update when the file does. However, if the file is replaced, it will keep tracking the old one.
	// If not writable, the compiler will let you write, but doing so will segfault at runtime.
	mmapw_t mmapw(bool writable = true) { return mmapw(writable, false); }
private:
	mmapw_t mmapw(bool writable, bool small)
	{
		mmapw_t ret;
		mmap_t::map(ret, *this, writable, small);
		return ret;
	}
public:
	static mmapw_t mmapw(cstrnul filename)
	{
		file2 f(filename, m_wr_existing);
		if (!f)
			return {};
		return f.mmapw();
	}
	
	// Resizes the file, and its associated mmap object. Same as unmapping, resizing and remapping, but optimizes slightly better.
	bool resize(off_t newsize, mmap_t& map) { return resize(newsize, (bytesr&)map, false, true); }
	bool resize(off_t newsize, mmapw_t& map, bool writable = true) { return resize(newsize, (bytesr&)map, writable, false); }
private:
	bool resize(off_t newsize, bytesr& map, bool writable, bool small);
public:
	
	// Flushes write() calls to disk. Normally done automatically after a second or so.
#ifndef __unix__
	void sync();
#else
	void sync() { fdatasync(fd); }
#endif
	
	// Flushes the mapped bytes to disk. Input must be mapped from this file object.
	// Would fit better directly on mmapw_t, but a full flush on Windows requires both FlushViewOfFile and FlushFileBuffers.
#ifndef __unix__
	void sync_map(const mmapw_t& map);
#else
	void sync_map(const mmapw_t& map) { msync((void*)map.ptr(), map.size(), MS_SYNC); }
#endif
	
	// Reads the entire file in a single function call. Generally not recommended, better use mmap().
	// Max size is 16MB.
	bytearray readall_array()
	{
		if (!*this)
			return {};
		size_t len = size();
		if (len == 0 || len > 16*1024*1024)
			return {};
		bytearray ret;
		ret.resize(len);
		if (read(ret) != len)
			return {};
		return ret;
	}
	static bytearray readall_array(cstrnul filename)
	{
		return file2(filename).readall_array();
	}
	
	class line_reader {
		file2* f;
		bytepipe by;
	public:
		line_reader(file2& f) : f(&f) {}
		cstring line();
	};
	
	// Will always end with a slash.
	static cstrnul dir_home();
	static cstrnul dir_config();
};


class file : nocopy {
public:
	class impl : nocopy {
	public:
		virtual size_t size() = 0;
		virtual bool resize(size_t newsize) = 0;
		
		virtual size_t pread(arrayvieww<uint8_t> target, size_t start) = 0;
		virtual bool pwrite(arrayview<uint8_t> data, size_t start = 0) = 0;
		virtual bool replace(arrayview<uint8_t> data) { return resize(data.size()) && (data.size() == 0 || pwrite(data)); }
		
		virtual array<uint8_t> readall()
		{
			array<uint8_t> ret;
			ret.reserve_noinit(this->size());
			size_t actual = this->pread(ret, 0);
			ret.resize(actual);
			return ret;
		}
		
		virtual arrayview<uint8_t> mmap(size_t start, size_t len) = 0;
		virtual void unmap(arrayview<uint8_t> data) = 0;
		virtual arrayvieww<uint8_t> mmapw(size_t start, size_t len) = 0;
		virtual bool unmapw(arrayvieww<uint8_t> data) = 0;
		
		virtual ~impl() {}
		
		arrayview<uint8_t> default_mmap(size_t start, size_t len);
		void default_unmap(arrayview<uint8_t> data);
		arrayvieww<uint8_t> default_mmapw(size_t start, size_t len);
		bool default_unmapw(arrayvieww<uint8_t> data);
	};
	
	class implrd : public impl {
	public:
		virtual size_t size() override = 0;
		bool resize(size_t newsize) override { return false; }
		
		virtual size_t pread(arrayvieww<uint8_t> target, size_t start) override = 0;
		bool pwrite(arrayview<uint8_t> data, size_t start = 0) override { return false; }
		bool replace(arrayview<uint8_t> data) override { return false; }
		
		virtual arrayview<uint8_t> mmap(size_t start, size_t len) override = 0;
		virtual void unmap(arrayview<uint8_t> data) override = 0;
		arrayvieww<uint8_t> mmapw(size_t start, size_t len) override { return NULL; }
		bool unmapw(arrayvieww<uint8_t> data) override { return false; }
	};
private:
	impl* core;
	size_t pos = 0;
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
	file(file&& f) { core = f.core; pos = f.pos; f.core = NULL; }
	file& operator=(file&& f) { delete core; core = f.core; f.core = NULL; pos = f.pos; return *this; }
	file(cstring filename, mode m = m_read) : core(NULL) { open(filename, m); }
	
	//A path refers to a directory if it ends with a slash, and file otherwise. Directories may not be open()ed.
	bool open(cstring filename, mode m = m_read)
	{
		delete core;
		core = open_impl(filename, m);
		pos = 0;
		return core;
	}
	void close()
	{
		delete core;
		core = NULL;
		pos = 0;
	}
	static file wrap(impl* core) { return file(core); }
	
private:
	//This one will create the file from the filesystem.
	//open_impl() can simply return open_impl_fs(filename), or can additionally support stuff like gvfs.
	static impl* open_impl_fs(cstring filename, mode m);
	static impl* open_impl(cstring filename, mode m);
public:
	
	operator bool() const { return core; }
	
	//Reading outside the file will return partial results.
	size_t size() const { return core->size(); }
	size_t pread(arrayvieww<uint8_t> target, size_t start) const { return core->pread(target, start); }
	size_t pread(array<uint8_t>& target, size_t start, size_t len) const { target.resize(len); return core->pread(target, start); }
	array<uint8_t> readall() const { return core->readall(); }
	static array<uint8_t> readall(cstring path)
	{
		file f(path);
		if (f) return f.readall();
		else return NULL;
	}
	string readallt() const { return readall(); }
	static string readallt(cstring path) { return readall(path); }
	
	bool resize(size_t newsize) { return core->resize(newsize); }
	//Writes outside the file will extend it with NULs.
	bool pwrite(arrayview<uint8_t> data, size_t pos = 0) { return core->pwrite(data, pos); }
	//File pointer is undefined after calling this.
	bool replace(arrayview<uint8_t> data) { return core->replace(data); }
	bool replace(cstring data) { return replace(data.bytes()); }
	bool pwrite(cstring data, size_t pos = 0) { return pwrite(data.bytes(), pos); }
	static bool writeall(cstring path, arrayview<uint8_t> data)
	{
		file f(path, m_replace);
		return f && f.pwrite(data);
	}
	static bool writeall(cstring path, cstring data) { return writeall(path, data.bytes()); }
	static bool replace_atomic(cstring path, arrayview<uint8_t> data);
	static bool replace_atomic(cstring path, cstring data) { return replace_atomic(path, data.bytes()); }
	
	//Seeking outside the file is fine. This will return short reads, or extend the file on write.
	bool seek(size_t pos) { this->pos = pos; return true; }
	size_t tell() { return pos; }
	size_t read(arrayvieww<uint8_t> data)
	{
		size_t ret = core->pread(data, pos);
		pos += ret;
		return ret;
	}
	array<uint8_t> read(size_t len)
	{
		array<uint8_t> ret;
		ret.resize(len);
		size_t newlen = core->pread(ret, pos);
		pos += newlen;
		ret.resize(newlen);
		return ret;
	}
	string readt(size_t len)
	{
		string ret;
		arrayvieww<uint8_t> bytes = ret.construct(len);
		size_t newlen = core->pread(bytes, pos);
		pos += newlen;
		
		if (newlen == bytes.size()) return ret;
		else return ret.substr(0, newlen);
	}
	bool write(arrayview<uint8_t> data)
	{
		bool ok = core->pwrite(data, pos);
		if (ok) pos += data.size();
		return ok;
	}
	bool write(cstring data) { return write(data.bytes()); }
	
	//Mappings are not guaranteed to update if the underlying file changes. To force an update, delete and recreate the mapping.
	//If the underlying file is changed while a written mapping exists, it's undefined which (if any) writes take effect.
	//Resizing the file while a mapping exists is undefined behavior, including if the mapping is still in bounds (memimpl doesn't like that).
	//Mappings must be deleted before deleting the file object.
	arrayview<uint8_t> mmap(size_t start, size_t len) const { return core->mmap(start, len); }
	arrayview<uint8_t> mmap() const { return this->mmap(0, this->size()); }
	void unmap(arrayview<uint8_t> data) const { return core->unmap(data); }
	
	arrayvieww<uint8_t> mmapw(size_t start, size_t len) { return core->mmapw(start, len); }
	arrayvieww<uint8_t> mmapw() { return this->mmapw(0, this->size()); }
	//If this succeeds, data written to the buffer is guaranteed to be written to the file, assuming no other writes were made in the region.
	//If not, file contents are undefined in that range.
	//TODO: remove return value, replace with ->sync()
	//if failure is detected, set a flag to fail sync()
	//actually, make all failures trip sync(), both read/write/unmapw
	bool unmapw(arrayvieww<uint8_t> data) { return core->unmapw(data); }
	
	~file() { delete core; }
	
	static file mem(arrayview<uint8_t> data)
	{
		return file(new file::memimpl(data));
	}
	//the array may not be modified while the file object exists, other than via the file object itself
	static file mem(array<uint8_t>& data)
	{
		return file(new file::memimpl(&data));
	}
private:
	class memimpl : public file::impl {
	public:
		arrayview<uint8_t> datard;
		array<uint8_t>* datawr; // even if writable, this object does not own the array
		
		memimpl(arrayview<uint8_t> data) : datard(data), datawr(NULL) {}
		memimpl(array<uint8_t>* data) : datard(*data), datawr(data) {}
		
		size_t size() override { return datard.size(); }
		bool resize(size_t newsize) override
		{
			if (!datawr) return false;
			datawr->resize(newsize);
			datard = *datawr;
			return true;
		}
		
		size_t pread(arrayvieww<uint8_t> target, size_t start) override
		{
			size_t nbyte = min(target.size(), datard.size()-start);
			memcpy(target.ptr(), datard.slice(start, nbyte).ptr(), nbyte);
			return nbyte;
		}
		bool pwrite(arrayview<uint8_t> newdata, size_t start = 0) override
		{
			if (!datawr) return false;
			size_t nbyte = newdata.size();
			datawr->reserve_noinit(start+nbyte);
			memcpy(datawr->slice(start, nbyte).ptr(), newdata.ptr(), nbyte);
			datard = *datawr;
			return true;
		}
		bool replace(arrayview<uint8_t> newdata) override
		{
			if (!datawr) return false;
			*datawr = newdata;
			datard = *datawr;
			return true;
		}
		
		arrayview<uint8_t>  mmap (size_t start, size_t len) override { return datard.slice(start, len); }
		arrayvieww<uint8_t> mmapw(size_t start, size_t len) override { if (!datawr) return NULL; return datawr->slice(start, len); }
		void  unmap(arrayview<uint8_t>  data) override {}
		bool unmapw(arrayvieww<uint8_t> data) override { return true; }
	};
public:
	
	static array<string> listdir(cstring path); // Returns all items in the given directory. All outputs are prefixed with the input.
	static bool mkdir(cstring path); // Returns whether that's now a directory. If it existed already, returns true; if a file, false.
	static bool unlink(cstring filename); // Returns whether the file is now gone. If the file didn't exist, returns true.
	
	static cstring dirname(cstring path) { return path.substr(0, path.lastindexof("/")+1); } // If the input path is a directory, the basename is blank.
	static cstring basename(cstring path) { return path.substr(path.lastindexof("/")+1, ~0); } // lastindexof returns -1 if nothing found, which works
	static cstrnul basename(cstrnul path) { return path.substr_nul(path.lastindexof("/")+1); }
	static const char * basename(const char * path) { const char * ret = strrchr(path, '/'); if (ret) return ret+1; else return path; }
	static string change_ext(cstring path, cstring new_ext); // new_ext should be ".bin" (can be blank)
	
	// Takes a byte sequence supposedly representing a relative file path from an untrusted source (for example a ZIP file).
	// If it's a normal, relative path, it's returned unchanged; if it's absolute or contains .. components, something else is returned.
	// The return value is a normal relative file path without .. or other surprises.
	// Output may contain backslashes (Linux only) and spaces. foo/bar/../baz/ does not necessarily get transformed to foo/baz/.
	static string sanitize_rel_path(string path);
	
	// Takes a byte sequence representing any file path from a trusted source (for example command line arguments),
	//  and returns it in a form usable by Arlib.
#ifdef _WIN32
	static string sanitize_trusted_path(cstring path) { return path.replace("\\", "/"); }
#else
	static cstring sanitize_trusted_path(cstring path) { return path; }
	static string sanitize_trusted_path(string path) { return path; }
#endif
	
	//Returns whether the path is absolute.
	//On Unix, absolute paths start with a slash.
	//On Windows:
	// Absolute paths start with two slashes, or letter+colon+slash.
	// Half-absolute paths, like /foo.txt or C:foo.txt, are considered corrupt and are undefined behavior.
	//The path component separator is the forward slash on all operating systems, including Windows.
	//Paths to directories end with a slash, paths to files do not. For example, /home/ and c:/windows/ are valid,
	// but /home and c:/windows are not.
	static bool is_absolute(cstring path)
	{
#if defined(__unix__)
		return path[0]=='/';
#elif defined(_WIN32)
		if (path[0]=='/' && path[1]=='/') return true;
		if (path[0]!='\0' && path[1]==':' && path[2]=='/') return true;
		return false;
#else
#error unimplemented
#endif
	}
	static bool path_corrupt(cstring path)
	{
		if (path.contains_nul()) return true;
		if (!path) return true;
#ifdef _WIN32
		if (path[0] == '/') return true; // TODO: this fails on network shares (\\?\, \\.\, and \??\ should be considered corrupt)
		if (path[1] == ':' && path[2] != '/') return true;
		if (path.contains("\\")) return true;
#endif
		return false;
	}
	
	//Removes all possible ./ and ../ components, and duplicate slashes, while still referring to the same file.
	//Similar to realpath(), but does not flatten symlinks.
	//foo/bar/../baz -> foo/baz, ./foo.txt -> foo.txt, ../foo.txt -> ../foo.txt, foo//bar.txt -> foo/bar.txt, . -> .
	//Invalid paths (above the root, or Windows half-absolute paths) are undefined behavior. Relative paths remain relative.
	static string resolve(cstring path);
	//Returns sub if it's absolute, else resolve(parent+sub). parent must end with a slash.
	static string resolve(cstring parent, cstring sub)
	{
		if (is_absolute(sub)) return resolve(sub);
		else return resolve(parent+sub);
	}
	
	// Returns the symlink target, as stored on disk. If not a symlink, returns empty string.
	static string readlink(cstrnul path);
	
	//Returns the location of the currently executing code, whether that's EXE or DLL.
	//May be blank if the path can't be determined. The cstring is owned by Arlib and lives forever.
	static cstrnul exepath();
	//Returns the directory of the above.
	static cstring exedir();
	//Returns the current working directory.
	static const string& cwd();
	
	// Does not resolve symlinks.
	static string realpath(cstring path) { return resolve(cwd(), path); }
private:
	static bool mkdir_fs(cstring filename);
	static bool unlink_fs(cstring filename);
};


class autommap : public arrayview<uint8_t> {
	const file& f;
public:
	autommap(const file& f, arrayview<uint8_t> b) : arrayview(b), f(f) {}
	autommap(const file& f, size_t start, size_t end) : arrayview(f.mmap(start, end)), f(f) {}
	autommap(const file& f) : arrayview(f.mmap()), f(f) {}
	~autommap() { f.unmap(*this); }
};

class autommapw : public arrayvieww<uint8_t> {
	file& f;
public:
	autommapw(file& f, arrayvieww<uint8_t> b) : arrayvieww(b), f(f) {}
	autommapw(file& f, size_t start, size_t end) : arrayvieww(f.mmapw(start, end)), f(f) {}
	autommapw(file& f) : arrayvieww(f.mmapw()), f(f) {}
	~autommapw() { f.unmapw(*this); }
};


class directory {
#ifdef __linux__
	int fd = -1;
	
	uint16_t buf_at;
	uint16_t buf_len;
	uint8_t buf[1024];
	
	bool fd_valid() { return fd >= 0; }
	
	// docs say this struct doesn't exist, but it does (but the dirent structure is bogus)
	// docs also say d_off is 64-bit offset to next structure, but that's also wrong; in reality, its only usecase is argument to lseek
	dirent64* current_dirent64()
	{
		return (dirent64*)(buf + buf_at);
	}
#endif
#ifdef _WIN32
	HANDLE fd = INVALID_HANDLE_VALUE;
	string path;
	WIN32_FIND_DATAA find;
	
	bool fd_valid() { return fd != INVALID_HANDLE_VALUE; }
#endif
	class dentry {
		directory* parent() { return container_of<&directory::dent>(this); }
		
	public:
		dentry() = default;
		dentry(const dentry&) = delete;
		
		cstrnul name;
		bool is_dir() { return parent()->dent_is_dir(); }
	};
	
	class iterator {
		directory* parent;
		friend class directory;
		
	public:
		iterator(directory* parent) : parent(parent) {}
		
		dentry& operator*() { return parent->iter_get(); }
		void operator++() { parent->iter_next(false); }
		bool operator!=(const end_iterator&) { return parent->iter_has(); }
	};
	
	dentry dent;
	
	bool dent_is_dir();
	
	dentry& iter_get()
	{
#ifdef __linux__
		const char * name = current_dirent64()->d_name;
#endif
#ifdef _WIN32
		const char * name = find.cFileName;
#endif
		dent.name = name;
		return dent;
	}
	
	void iter_next(bool initial);
	bool iter_has()
	{
#ifdef __linux__
		return fd >= 0 && buf_len != 0;
#endif
#ifdef _WIN32
		return fd != INVALID_HANDLE_VALUE;
#endif
	}
	
	void close_me()
	{
#ifdef __linux__
		if (fd >= 0)
			close(fd);
#endif
#ifdef _WIN32
		if (fd != INVALID_HANDLE_VALUE)
			FindClose(fd);
#endif
	}
	
	void usurp(directory& other)
	{
#ifdef __linux__
		fd = other.fd;
		other.fd = -1;
		
		buf_at = other.buf_at;
		buf_len = other.buf_len;
		memcpy(buf, other.buf, buf_len);
#endif
#ifdef _WIN32
		fd = other.fd;
		other.fd = INVALID_HANDLE_VALUE;
		
		path = std::move(other.path);
		find = other.find;
#endif
	}
	
#ifdef __linux__
	directory(int fd) : fd(fd) { iter_next(true); }
#endif
	
	file2 open_file_inner(const char * path, file2::mode m = file2::m_read);
	directory open_dir_inner(const char * path);
public:
	directory() {}
	directory(cstrnul path);
	directory(const directory&) = delete;
	directory(directory&& other) { usurp(other); }
	directory& operator=(const directory&) = delete;
	directory& operator=(directory&& other) { close_me(); usurp(other); return *this; }
	~directory() { close_me(); }
	operator bool() { return fd_valid(); }
	
	file2 open_file(const dentry& dent, file2::mode m = file2::m_read)
	{
		return open_file_inner(dent.name, m);
	}
	file2 open_file(cstrnul path, file2::mode m = file2::m_read)
	{
		if (file::path_corrupt(path))
			return {};
		return open_file_inner(path, m);
	}
	directory open_dir(const dentry& dent)
	{
		return open_dir_inner(dent.name);
	}
	directory open_dir(cstrnul path)
	{
		if (file::path_corrupt(path))
			return {};
		return open_dir_inner(path);
	}
	
	// Each directory object can only be iterated once. If you need to iterate again, use open_dir(".").
	iterator begin() { return this; }
	end_iterator end() { return {}; }
};

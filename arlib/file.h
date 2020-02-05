#pragma once
#include "global.h"
#include "string.h"
#include "array.h"

// TODO: this class needs to be rewritten
// commonly needed usecases, like file::readall, should not depend on unnecessary functionality, like seeking
// (/dev/stdin is unseekable, and gvfs http seek sometimes fails)
//
// usecases:
// read all, read streaming, read random
// replace contents, replace/append streaming, rw random
// the above six but async
// mmap read, mmap rw
// read all and flock()
//
// read all should, as now, be a single function, but implemented in the backend
//  since typical usecase is passing return value to e.g. JSON::parse, it should return normal array<byte> or string
// replace should also be a backend function, but it should be atomic, initially writing to filename+".aswptmp" (remember to fsync)
//  ideally, it'd be open(O_TMPFILE)+linkat(O_REPLACE), but kernel doesn't allow that
// mmap shouldn't be in the file object either, but take a filename and return an arrayview(w)<byte> child with a dtor
// read-and-lock should also be a function, also returning an arrayview<byte> child with a dtor
//  this one should also have a 'replace contents' member, still atomic (flock the new one before renaming)
// async can be ignored for now; it's rarely useful, async local file needs threads on linux, and async gvfs requires glib runloop
// the other four combinations belong in the file object; replace/append streaming is useful for logs
//  they should use seek/read/size as primitives, not pread

class file2 : nocopy {
	file2() = delete;
public:
	//arrayview<byte> readall()
	//cstring readallt()
	
	class mmap_t : public arrayview<byte>, nocopy {
		friend class file2;
		mmap_t(nullptr_t) {}
		mmap_t(const uint8_t * ptr, size_t count) : arrayview(ptr, count) {}
	public:
		mmap_t(const mmap_t&) = delete;
		mmap_t(mmap_t&& other) : arrayview(other) {other.items = nullptr; other.count = 0; }
		~mmap_t();
	};
	
	static mmap_t mmap(cstring filename);
	//static mmapw_t mmapw(cstring filename);
};

class file : nocopy {
public:
	class impl : nocopy {
	public:
		virtual size_t size() = 0;
		virtual bool resize(size_t newsize) = 0;
		
		virtual size_t pread(arrayvieww<byte> target, size_t start) = 0;
		virtual bool pwrite(arrayview<byte> data, size_t start = 0) = 0;
		virtual bool replace(arrayview<byte> data) { return resize(data.size()) && (data.size() == 0 || pwrite(data)); }
		
		virtual array<byte> readall()
		{
			array<byte> ret;
			ret.reserve_noinit(this->size());
			size_t actual = this->pread(ret, 0);
			ret.resize(actual);
			return ret;
		}
		
		virtual arrayview<byte> mmap(size_t start, size_t len) = 0;
		virtual void unmap(arrayview<byte> data) = 0;
		virtual arrayvieww<byte> mmapw(size_t start, size_t len) = 0;
		virtual bool unmapw(arrayvieww<byte> data) = 0;
		
		virtual ~impl() {}
		
		arrayview<byte> default_mmap(size_t start, size_t len);
		void default_unmap(arrayview<byte> data);
		arrayvieww<byte> default_mmapw(size_t start, size_t len);
		bool default_unmapw(arrayvieww<byte> data);
	};
	
	class implrd : public impl {
	public:
		virtual size_t size() = 0;
		bool resize(size_t newsize) { return false; }
		
		virtual size_t pread(arrayvieww<byte> target, size_t start) = 0;
		bool pwrite(arrayview<byte> data, size_t start = 0) { return false; }
		bool replace(arrayview<byte> data) { return false; }
		
		virtual arrayview<byte> mmap(size_t start, size_t len) = 0;
		virtual void unmap(arrayview<byte> data) = 0;
		arrayvieww<byte> mmapw(size_t start, size_t len) { return NULL; }
		bool unmapw(arrayvieww<byte> data) { return false; }
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
	size_t pread(arrayvieww<byte> target, size_t start) const { return core->pread(target, start); }
	array<byte> readall() const { return core->readall(); }
	static array<byte> readall(cstring path)
	{
		file f(path);
		if (f) return f.readall();
		else return NULL;
	}
	string readallt() const { return readall(); }
	static string readallt(cstring path) { return readall(path); }
	
	bool resize(size_t newsize) { return core->resize(newsize); }
	//Writes outside the file will extend it with NULs.
	bool pwrite(arrayview<byte> data, size_t pos = 0) { return core->pwrite(data, pos); }
	//File pointer is undefined after calling this.
	bool replace(arrayview<byte> data) { return core->replace(data); }
	bool replace(cstring data) { return replace(data.bytes()); }
	bool pwrite(cstring data, size_t pos = 0) { return pwrite(data.bytes(), pos); }
	static bool writeall(cstring path, arrayview<byte> data)
	{
		file f(path, m_replace);
		return f && f.pwrite(data);
	}
	static bool writeall(cstring path, cstring data) { return writeall(path, data.bytes()); }
	static bool replace_atomic(cstring path, arrayview<byte> data);
	static bool replace_atomic(cstring path, cstring data) { return replace_atomic(path, data.bytes()); }
	
	//Seeking outside the file is fine. This will return short reads, or extend the file on write.
	bool seek(size_t pos) { this->pos = pos; return true; }
	size_t tell() { return pos; }
	size_t read(arrayvieww<byte> data)
	{
		size_t ret = core->pread(data, pos);
		pos += ret;
		return ret;
	}
	array<byte> read(size_t len)
	{
		array<byte> ret;
		ret.resize(len);
		size_t newlen = core->pread(ret, pos);
		pos += newlen;
		ret.resize(newlen);
		return ret;
	}
	string readt(size_t len)
	{
		string ret;
		arrayvieww<byte> bytes = ret.construct(len);
		size_t newlen = core->pread(bytes, pos);
		pos += newlen;
		
		if (newlen == bytes.size()) return ret;
		else return ret.substr(0, newlen);
	}
	bool write(arrayview<byte> data)
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
	arrayview<byte> mmap(size_t start, size_t len) const { return core->mmap(start, len); }
	arrayview<byte> mmap() const { return this->mmap(0, this->size()); }
	void unmap(arrayview<byte> data) const { return core->unmap(data); }
	
	arrayvieww<byte> mmapw(size_t start, size_t len) { return core->mmapw(start, len); }
	arrayvieww<byte> mmapw() { return this->mmapw(0, this->size()); }
	//If this succeeds, data written to the file is guaranteed to be written, assuming no other writes were made in the region.
	//If not, file contents are undefined in that range.
	//TODO: remove return value, replace with ->sync()
	//if failure is detected, set a flag to fail sync()
	//actually, make all failures trip sync(), both read/write/unmapw
	bool unmapw(arrayvieww<byte> data) { return core->unmapw(data); }
	
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
	class memimpl : public file::impl {
	public:
		arrayview<byte> datard;
		array<byte>* datawr; // even if writable, this object does not own the array
		
		memimpl(arrayview<byte> data) : datard(data), datawr(NULL) {}
		memimpl(array<byte>* data) : datard(*data), datawr(data) {}
		
		size_t size() { return datard.size(); }
		bool resize(size_t newsize)
		{
			if (!datawr) return false;
			datawr->resize(newsize);
			datard = *datawr;
			return true;
		}
		
		size_t pread(arrayvieww<byte> target, size_t start)
		{
			size_t nbyte = min(target.size(), datard.size()-start);
			memcpy(target.ptr(), datard.slice(start, nbyte).ptr(), nbyte);
			return nbyte;
		}
		bool pwrite(arrayview<byte> newdata, size_t start = 0)
		{
			if (!datawr) return false;
			size_t nbyte = newdata.size();
			datawr->reserve_noinit(start+nbyte);
			memcpy(datawr->slice(start, nbyte).ptr(), newdata.ptr(), nbyte);
			datard = *datawr;
			return true;
		}
		bool replace(arrayview<byte> newdata)
		{
			if (!datawr) return false;
			*datawr = newdata;
			datard = *datawr;
			return true;
		}
		
		arrayview<byte>   mmap(size_t start, size_t len) { return datard.slice(start, len); }
		arrayvieww<byte> mmapw(size_t start, size_t len) { if (!datawr) return NULL; return datawr->slice(start, len); }
		void  unmap(arrayview<byte>  data) {}
		bool unmapw(arrayvieww<byte> data) { return true; }
	};
public:
	
	static array<string> listdir(cstring path); // Returns all items in the given directory. All outputs are prefixed with the input.
	static bool mkdir(cstring path); // Returns whether that's now a directory. If it existed already, returns true; if a file, false.
	static bool unlink(cstring filename); // Returns whether the file is now gone. If the file didn't exist, returns true.
	static string dirname(cstring path){ return path.rsplit<1>("/")[0]+"/"; } //If the input path is a directory, the basename is blank.
	static string basename(cstring path) { return path.rsplit<1>("/")[1]; }
	static string change_ext(cstring path, cstring new_ext); // new_ext should be ".bin" (can be blank)
	
	//Arlib does not recognize backslashes in filenames as legitimate.
#ifdef _WIN32
	static string sanitize_path(cstring path) { return path.replace("\\", "/"); }
#else
	static cstring sanitize_path(cstring path) { return path; }
	static string sanitize_path(string path) { return path; }
#endif
	
	//Returns whether the path is absolute.
	//On Unix, absolute paths start with a slash.
	//On Windows:
	// Absolute paths start with two slashes, or letter+colon+slash.
	// Half-absolute paths, like /foo.txt or C:foo.txt on Windows, are considered corrupt and are undefined behavior.
	//The path component separator is the forward slash on all operating systems, including Windows.
	//Paths to directories end with a slash, Paths to files do not. For example, /home/ and c:/windows/ are valid,
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
	
	//Removes all possible ./ and ../ components, and duplicate slashes, while still referring to the same file.
	//Similar to realpath(), but does not flatten symlinks.
	//foo/bar/../baz -> foo/baz, ./foo.txt -> foo.txt, ../foo.txt -> ../foo.txt, foo//bar.txt -> foo/bar.txt, . -> .
	//Invalid paths (above the root, or Windows half-absolute paths) are undefined behavior. Relative paths remain relative.
	static string resolve(cstring path);
	//Returns sub if it's absolute, else resolve(parent+sub). parent must end with a slash.
	static string resolve(cstring parent, cstring sub)
	{
		if (is_absolute(sub)) return sub;
		else return resolve(parent+sub);
	}
	
	//Returns the path of the executable, including filename.
	//The cstring is owned by Arlib and lives forever.
	static cstring exepath();
	//Returns the current working directory.
	static cstring cwd();
private:
	static bool mkdir_fs(cstring filename);
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

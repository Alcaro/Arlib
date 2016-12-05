#include "file.h"
#include "test.h"

//TODO: to test:
#if 0
//file
	static file* open(cstring filename);
	static array<string> listdir(cstring path);
	
	static string dirname(cstring path);
	static string basename(cstring path);
	
	string path;
	size_t len;
	
	virtual size_t read(array<byte>& target, size_t start) = 0;

	virtual arrayview<byte> mmap(size_t start, size_t len) = 0;
	virtual void unmap(arrayview<byte> data) = 0;

//filewrite
	enum mode {
		m_default,        // If the file exists, opens it. If it doesn't, creates a new file.     (O_CREAT)         (OPEN_ALWAYS)
		m_existing,       // Fails if the file doesn't exist.                                     (0)               (OPEN_EXISTING)
		m_replace,        // If the file exists, it's either deleted and recreated, or truncated. (O_CREAT|O_TRUNC) (CREATE_ALWAYS)
		m_create_excl,    // Fails if the file does exist.                                        (O_CREAT|O_EXCL)  (CREATE_NEW)
	};

	static filewrite* open(cstring filename, mode m = m_default);
	static bool unlink(cstring filename);
	
	virtual bool resize(size_t newsize) = 0;
	virtual bool write(arrayview<byte> data, size_t start = 0) = 0;
	virtual bool replace(arrayview<byte> data) { return resize(data.size()) && write(data); }
	
	virtual arrayvieww<byte> mmapw(size_t start, size_t len) = 0;
	arrayvieww<byte> mmapw() { return this->mmapw(0, this->len); }
	virtual void unmapw(arrayvieww<byte> data) = 0;
#endif

//criteria:
//- must be a normal file, no /dev/*
//- minimum 66000 bytes
//- the first few bytes must be known, no .txt files or possibly-shebanged stuff
//- the file must contain somewhat unpredictable data, nothing from /dev/zero
//- must be readable by everyone (assuming absense of sandboxes)
//- must NOT be writable or deletable by this program
//recommended choice: some random executable
#ifdef _WIN32
#define READONLY_FILE "C:/Windows/notepad.exe" // screw anything where the windows directory isn't on C:
#define READONLY_FILE_HEAD "MZ"
#else
#define READONLY_FILE "/bin/sh"
#define READONLY_FILE_HEAD "\x7F""ELF"
#endif

//criteria:
//- no funny symbols
//- implausible name, nothing of value must be lost by deleting it
#define WRITABLE_FILE "arlib-selftest.txt"

test("file reading")
{
	autoptr<file> f = file::open(READONLY_FILE);
	assert(f);
	assert(f->len);
	assert(f->len > strlen(READONLY_FILE_HEAD));
	assert(f->len >= 66000);
	array<byte> bytes = f->read();
	assert(bytes.size() == f->len);
	assert(!memcmp(bytes.ptr(), READONLY_FILE_HEAD, strlen(READONLY_FILE_HEAD)));
	
	arrayview<byte> map = f->mmap();
	assert(map.ptr());
	assert(map.size() == f->len);
	assert(!memcmp(bytes.ptr(), map.ptr(), bytes.size()));
	
	arrayview<byte> map2 = f->mmap();
	assert(map2.ptr());
	assert(map2.size() == f->len);
	assert(!memcmp(bytes.ptr(), map2.ptr(), bytes.size()));
	f->unmap(map2);
	
	const size_t t_start[] = { 0,     65536, 4096, 1,     1,     1,     65537, 65535 };
	const size_t t_len[]   = { 66000, 400,   400,  65535, 65536, 65999, 400,   2     };
	for (size_t i=0;i<ARRAY_SIZE(t_start);i++)
	{
		arrayview<byte> map3 = f->mmap(t_start[i], t_len[i]);
		assert(map3.ptr());
		assert(map3.size() == t_len[i]);
		assert(!memcmp(bytes.ptr()+t_start[i], map3.ptr(), t_len[i]));
		f->unmap(map3);
	}
	
	f->unmap(map);
}

test("file writing")
{
	autoptr<filewrite> f;
	
	assert(!filewrite::open(READONLY_FILE, filewrite::m_default));
	assert(!filewrite::open(READONLY_FILE, filewrite::m_existing));
	assert(!filewrite::open(READONLY_FILE, filewrite::m_replace));
	assert(!filewrite::open(READONLY_FILE, filewrite::m_create_excl));
	
	assert(filewrite::unlink(WRITABLE_FILE));
	
	assert(!file::open(WRITABLE_FILE));
	
	f = filewrite::open(WRITABLE_FILE);
	assert(f);
	assert(f->replace("foo"));
	
	assert_eq(string(file::read(WRITABLE_FILE)), "foo");
	
	f->resize(8);
	byte expected[8]={'f','o','o',0,0,0,0,0};
	array<byte> actual = file::read(WRITABLE_FILE);
	assert(actual.ptr());
	assert(actual.size()==8);
	assert(!memcmp(actual.ptr(), expected, 8));
	
	arrayvieww<byte> map = f->mmapw();
	assert(map.ptr());
	assert_eq(map.size(), 8);
	assert(!memcmp(map.ptr(), expected, 8));
	map[3]='t';
	f->unmapw(map);
	
	expected[3] = 't';
	actual = file::read(WRITABLE_FILE);
	assert(actual.ptr());
	assert(actual.size()==8);
	assert(!memcmp(actual.ptr(), expected, 8));
	
	f = NULL;
	
	//test the various creation modes
	//file exists, these three should work
	f=NULL; assert( (f=filewrite::open(WRITABLE_FILE, filewrite::m_default)));
	f=NULL; assert( (f=filewrite::open(WRITABLE_FILE, filewrite::m_existing)));
	assert_eq(f->len, 8);
	f=NULL; assert( (f=filewrite::open(WRITABLE_FILE, filewrite::m_replace)));
	assert_eq(f->len, 0);
	f=NULL; assert(!(f=filewrite::open(WRITABLE_FILE, filewrite::m_create_excl)));//but this shouldn't
	
	f=NULL;
	assert(filewrite::unlink(WRITABLE_FILE));
	assert(!filewrite::open(WRITABLE_FILE, filewrite::m_existing)); // this should fail
	f=NULL; assert(f=filewrite::open(WRITABLE_FILE, filewrite::m_create_excl)); // this should create
	assert(filewrite::unlink(WRITABLE_FILE));
	
	f=NULL; assert(f=filewrite::open(WRITABLE_FILE, filewrite::m_replace)); // replacing a nonexistent file is fine
	//opening a nonexistent file with m_default is tested at the start of this function
	f=NULL;
	assert(filewrite::unlink(WRITABLE_FILE));
	assert(filewrite::unlink(WRITABLE_FILE)); // ensure it properly deals with unlinking a nonexistent file
}

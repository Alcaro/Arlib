#pragma once
#include "array.h"

//A bytepipe accepts an infinite amount of bytes, and returns them, first one first.
class bytepipe {
	array<byte> buf1;
	size_t buf1st;
	size_t buf1end;
	
	array<byte> buf2;
	size_t buf2end;
	
	void try_swap()
	{
		if (buf1st == buf1end)
		{
			buf1.swap(buf2);
			buf1st = 0;
			buf1end = buf2end;
			buf2end = 0;
			if (buf2.size() > 65536) buf2.resize(65536);
		}
	}
	
	void push_one(arrayview<byte> bytes)
	{
		arrayvieww<byte> tmp = push_buf(bytes.size());
		memcpy(tmp.ptr(), bytes.ptr(), bytes.size());
		push_done(bytes.size());
	}
	void push_one(cstring str)
	{
		push(str.bytes());
	}
	void push() {}
	
public:
	bytepipe()
	{
		reset();
	}
	
	//Will return a buffer of at least 'bytes' bytes. Can return more.
	arrayvieww<byte> push_buf(size_t bytes = 512)
	{
		if (buf2end + bytes > buf2.size())
		{
			try_swap();
		}
		if (buf2end + bytes > buf2.size())
		{
			size_t newsize = buf2.size();
			while (buf2end + bytes > newsize) newsize *= 2;
			buf2.resize(newsize);
		}
		return buf2.skip(buf2end);
	}
	void push_done(size_t bytes)
	{
		buf2end += bytes;
	}
	
	template<typename T, typename... Tnext> void push(T first, Tnext... next)
	{
		push_one(first);
		push(next...);
	}
	
	//Can return less than remaining().
	arrayview<byte> pull_buf()
	{
		try_swap();
		return buf1.slice(buf1st, buf1end-buf1st);
	}
	//Returns whatever was pushed that pull_buf didn't return. Can't be acknowledged and discarded, use pull_buf.
	arrayview<byte> pull_next()
	{
		return buf2.slice(0, buf2end);
	}
	void pull_done(size_t bytes)
	{
		buf1st += bytes;
	}
	//Returns the entire thing.
	arrayview<byte> pull_buf_full()
	{
		if (buf1end+buf2end > buf1.size())
		{
			memmove(buf1.ptr(), buf1.slice(buf1st, buf1end-buf1st).ptr(), buf1end-buf1st);
			buf1end -= buf1st;
			buf1st = 0;
			if (buf1end+buf2end > buf1.size())
			{
				if (buf1end > buf2end) buf1.resize(buf1.size()*2);
				else buf1.resize(buf2.size()*2);
			}
		}
		
		memcpy(buf1.slice(buf1end, buf2end).ptr(), buf2.ptr(), buf2end);
		buf1end += buf2end;
		buf2end = 0;
		
		return buf1.slice(buf1st, buf1end-buf1st);
	}
	
	size_t remaining() { return buf1end-buf1st+buf2end; }
	void reset()
	{
		buf1.resize(1024);
		buf1st = 0;
		buf1end = 0;
		buf2.resize(1024);
		buf2end = 0;
	}
};

#pragma once
#include "array.h"
#include "endian.h"

//prefers little endian, it's more common
//you're welcome to inherit this object if you need a more rare operation, like leb128
class bytestream {
protected:
	const uint8_t* start;
	const uint8_t* at;
	const uint8_t* end;
	
public:
	bytestream(arrayview<uint8_t> buf) : start(buf.ptr()), at(buf.ptr()), end(buf.ptr()+buf.size()) {}
	bytestream(const bytestream& other) : start(other.start), at(other.at), end(other.at) {}
	bytestream() : start(NULL), at(NULL), end(NULL) {}
	
	arrayview<uint8_t> bytes(size_t n)
	{
		arrayview<uint8_t> ret = arrayview<uint8_t>(at, n);
		at += n;
		return ret;
	}
	arrayview<uint8_t> peekbytes(size_t n)
	{
		return arrayview<uint8_t>(at, n);
	}
	bool signature(cstring sig)
	{
		arrayview<uint8_t> expected = sig.bytes();
		arrayview<uint8_t> actual = peekbytes(sig.length());
		if (actual == expected)
		{
			bytes(sig.length());
			return true;
		}
		else return false;
	}
	uint8_t u8()
	{
		return *(at++);
	}
	int u8_or(int otherwise)
	{
		if (at==end) return otherwise;
		return *(at++);
	}
	uint16_t u16()
	{
		return end_nat_to_le(bytes(2).reinterpret<uint16_t>()[0]);
	}
	uint16_t u16be()
	{
		return end_nat_to_be(bytes(2).reinterpret<uint16_t>()[0]);
	}
	uint32_t u32()
	{
		return end_nat_to_le(bytes(4).reinterpret<uint32_t>()[0]);
	}
	uint32_t u32be()
	{
		return end_nat_to_be(bytes(4).reinterpret<uint32_t>()[0]);
	}
	uint32_t u32at(size_t pos)
	{
		return end_nat_to_le(*(uint32_t*)(start+pos));
	}
	uint32_t u32beat(size_t pos)
	{
		return end_nat_to_be(*(uint32_t*)(start+pos));
	}
	
	size_t pos() { return at-start; }
	size_t size() { return end-start; }
	size_t remaining() { return end-at; }
	
	uint32_t u24()
	{
		//doubt this is worth optimizing, probably rare...
		arrayview<uint8_t> b = bytes(3);
		return b[0] | b[1]<<8 | b[2]<<16;
	}
	uint32_t u24be()
	{
		return end_swap24(u24());
	}
};

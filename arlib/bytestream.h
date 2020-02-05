#pragma once
#include "array.h"
#include "endian.h"
#include "string.h"

//you're welcome to extend this object if you need a more rare operation, like leb128
//signature and u8_or check for otherflow; for anything else, use remaining()
class bytestream {
protected:
	const uint8_t* start;
	const uint8_t* at;
	const uint8_t* end;
	
public:
	bytestream(arrayview<uint8_t> buf)  { init(buf); }
	bytestream(const bytestream& other) : start(other.start), at(other.at), end(other.end) {}
	bytestream() : start(NULL), at(NULL), end(NULL) {}
	
	void init(arrayview<uint8_t> buf)
	{
		start = buf.ptr();
		at = buf.ptr();
		end = buf.ptr()+buf.size();
	}
	
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
		if (sig.length() > remaining())
			return false;
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
	uint8_t u8l() { return u8(); }
	uint8_t u8b() { return u8(); }
	uint16_t u16l() { return readu_le16(bytes(2).ptr()); }
	uint16_t u16b() { return readu_be16(bytes(2).ptr()); }
	uint32_t u32l() { return readu_le32(bytes(4).ptr()); }
	uint32_t u32b() { return readu_be32(bytes(4).ptr()); }
	uint32_t u64l() { return readu_le64(bytes(8).ptr()); }
	uint32_t u64b() { return readu_be64(bytes(8).ptr()); }
	
	float f32l()
	{
		static_assert(sizeof(float) == 4);
		uint32_t a = end_le_to_nat(u32l());
		float b;
		memcpy(&b, &a, sizeof(float));
		return b;
	}
	float f32b()
	{
		static_assert(sizeof(float) == 4);
		uint32_t a = end_be_to_nat(u32l());
		float b;
		memcpy(&b, &a, sizeof(float));
		return b;
	}
	double f64l()
	{
		static_assert(sizeof(double) == 8);
		uint64_t a = end_le_to_nat(u64l());
		double b;
		memcpy(&b, &a, sizeof(double));
		return b;
	}
	double f64b()
	{
		static_assert(sizeof(double) == 8);
		uint64_t a = end_be_to_nat(u64l());
		double b;
		memcpy(&b, &a, sizeof(double));
		return b;
	}
	
	cstring strnul()
	{
		const uint8_t * tmp = at;
		while (*at) at++;
		return cstring(arrayview<uint8_t>(tmp, (at++)-tmp));
	}
	
	size_t tell() { return at-start; }
	size_t size() { return end-start; }
	size_t remaining() { return end-at; }
	
	void seek(size_t pos) { at = start+pos; }
	void skip(ssize_t off) { at += off; }
	
	uint32_t u32lat(size_t pos)
	{
		return readu_le32(start+pos);
	}
	uint32_t u32bat(size_t pos)
	{
		return readu_be32(start+pos);
	}
	
	arrayview<uint8_t> peek_at(size_t pos, size_t len) { return arrayview<uint8_t>(start+pos, len); }
	
	uint32_t u24l()
	{
		//doubt this is worth optimizing, probably rare...
		arrayview<uint8_t> b = bytes(3);
		return b[0] | b[1]<<8 | b[2]<<16;
	}
	uint32_t u24b()
	{
		return end_swap24(u24l());
	}
};

class bytestreame : public bytestream {
protected:
	bool big_endian = false;
	
public:
	bytestreame(arrayview<uint8_t> buf, bool big_endian) : bytestream(buf), big_endian(big_endian) {}
	bytestreame(arrayview<uint8_t> buf) : bytestream(buf) {}
	bytestreame(const bytestreame& other) = default;
	bytestreame() {}
	
	void set_big_endian(bool big) { big_endian = big; }
	
	uint16_t u16() { return big_endian ? u16b() : u16l(); }
	uint32_t u32() { return big_endian ? u32b() : u32l(); }
	uint64_t u64() { return big_endian ? u64b() : u64l(); }
	float    f32() { return big_endian ? f32b() : f32l(); }
	double   f64() { return big_endian ? f64b() : f64l(); }
};

class bytestreamw {
protected:
	array<byte> buf;
	
public:
	void bytes(arrayview<byte> data)
	{
		buf += data;
	}
	void text(cstring str)
	{
		buf += str.bytes();
	}
	void u8(uint8_t val)
	{
		buf += arrayview<byte>(&val, 1);
	}
	void u16l(uint16_t val)
	{
		litend<uint16_t> valn = val;
		buf += valn.bytes();
	}
	void u16b(uint16_t val)
	{
		bigend<uint16_t> valn = val;
		buf += valn.bytes();
	}
	void u24l(uint32_t val)
	{
		u8(val>>0);
		u8(val>>8);
		u8(val>>16);
	}
	void u24b(uint32_t val)
	{
		u8(val>>16);
		u8(val>>8);
		u8(val>>0);
	}
	void u32l(uint32_t val)
	{
		litend<uint32_t> valn = val;
		buf += valn.bytes();
	}
	void u32b(uint32_t val)
	{
		bigend<uint32_t> valn = val;
		buf += valn.bytes();
	}
	void u64l(uint64_t val)
	{
		litend<uint64_t> valn = val;
		buf += valn.bytes();
	}
	void u64b(uint64_t val)
	{
		bigend<uint64_t> valn = val;
		buf += valn.bytes();
	}
	
	void f32l(float val)
	{
		static_assert(sizeof(float) == 4);
		uint32_t ival;
		memcpy(&ival, &val, sizeof(float));
		ival = end_nat_to_le(ival);
		buf += arrayview<byte>((uint8_t*)&ival, sizeof(uint32_t));
	}
	void f32b(float val)
	{
		static_assert(sizeof(float) == 4);
		uint32_t ival;
		memcpy(&ival, &val, sizeof(float));
		ival = end_nat_to_be(ival);
		buf += arrayview<byte>((uint8_t*)&ival, sizeof(uint32_t));
	}
	void f64l(double val)
	{
		static_assert(sizeof(double) == 8);
		uint64_t ival;
		memcpy(&ival, &val, sizeof(double));
		ival = end_nat_to_le(ival);
		buf += arrayview<byte>((uint8_t*)&ival, sizeof(uint64_t));
	}
	void f64b(double val)
	{
		static_assert(sizeof(double) == 8);
		uint64_t ival;
		memcpy(&ival, &val, sizeof(double));
		ival = end_nat_to_be(ival);
		buf += arrayview<byte>((uint8_t*)&ival, sizeof(uint64_t));
	}
	
	arrayview<byte> peek()
	{
		return buf;
	}
	array<byte> finish()
	{
		return std::move(buf);
	}
};

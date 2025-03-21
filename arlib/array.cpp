#include "array.h"

void bitarray::set_slice(size_t start, size_t num, const bitarray& other, size_t other_start)
{
	if (&other == this)
	{
		//TODO: optimize
		bitarray copy = other;
		set_slice(start, num, copy, other_start);
		return;
	}
	//TODO: optimize
	for (size_t i=0;i<num;i++)
	{
		set(start+i, other.get(other_start+i));
	}
}

void bitarray::clear_unused(size_t start, size_t nbytes)
{
	size_t low = start;
	size_t high = nbytes*8;
	
	if (low == high) return; // don't wipe bits()[8] if they're both 64
	
	chunk_t* ptr = bits();
	chunk_t& byte = ptr[low/chunk_size];
	byte &=~ (chunk_t(-1)<<(low&(chunk_size-1)));
	low = (low+(chunk_size-1))&~(chunk_size-1);
	
	memset(ptr+low/chunk_size, 0, (high-low)/chunk_size*sizeof(chunk_t));
}

void bitarray::resize(size_t len)
{
	size_t prevlen = this->nbits;
	this->nbits = len;
	
	switch ((prevlen > n_inline)<<1 | (len > n_inline))
	{
	case 0: // small->small
		if (len < prevlen)
			clear_unused(this->nbits, sizeof(this->bits_inline));
		break;
	case 1: // small->big
		{
			size_t newbytes = alloc_size(len);
			chunk_t* newbits = xmalloc(newbytes);
			memcpy(newbits, this->bits_inline, sizeof(this->bits_inline));
			memset(newbits+ARRAY_SIZE(this->bits_inline), 0, newbytes-sizeof(this->bits_inline));
			bits_outline = newbits;
		}
		break;
	case 2: // big->small
		{
			chunk_t* freethis = this->bits_outline;
			memcpy(this->bits_inline, this->bits_outline, sizeof(this->bits_inline));
			free(freethis);
			clear_unused(this->nbits, sizeof(this->bits_inline));
		}
		break;
	case 3: // big->big
		{
			size_t prevbytes = alloc_size(prevlen);
			size_t newbytes = alloc_size(len);
			if (newbytes > prevbytes)
			{
				bits_outline = xrealloc(this->bits_outline, newbytes);
				clear_unused(prevlen, newbytes);
			}
			else if (len < prevlen)
			{
				clear_unused(this->nbits, newbytes);
			}
		}
		break;
	}
	
//printf("%lu->%lu t=%d", prevlen, len, ((prevlen > n_inline)<<1 | (len > n_inline)));
//size_t bytes;
//if (len > n_inline) bytes = alloc_size(len);
//else bytes = sizeof(bits_inline);
//const uint8_t * bits8 = (uint8_t*)bits();
//for (size_t i=0;i<bitround(bytes);i++)
//{
//printf(" %.2X", bits8[i]);
//}
//puts("");
//bool fail=false;
//if (len>prevlen)
//{
//for (size_t n=prevlen;n<len;n++)
//{
//	if (get(n))
//	{
//		printf("%lu->%lu: unexpected at %lu\n", prevlen, len, n);
//		fail=true;
//	}
//}
//}
//if(fail)abort();
}

bitarray bitarray::slice(size_t first, size_t count) const
{
	if ((first&7) == 0)
	{
		bitarray ret;
		ret.resize(count);
		memcpy(ret.bits(), this->bits() + first/8, (count+7)/8);
		return ret;
	}
	else
	{
		//TODO: optimize
		bitarray ret;
		ret.resize(count);
		for (size_t i=0;i<count;i++) ret.set(i, this->get(first+i));
		return ret;
	}
}

#include "test.h"

#ifdef ARLIB_TEST
test("array", "", "array")
{
	assert_eq(sizeof(array<int>), sizeof(int*)+sizeof(size_t));
	
	{
		array<int> x = { 1, 2, 3 };
		assert_eq(x[0], 1);
		assert_eq(x[1], 2);
		assert_eq(x[2], 3);
	}
	
	{
		array<int> x = { 1, 2, 3 };
		assert_eq(x.pop(1), 2);
		assert_eq(x[0], 1);
		assert_eq(x[1], 3);
	}
	
	{
		//passes if it does not leak memory
		class glutton {
			array<uint8_t> food;
		public:
			glutton() { food.resize(1000); }
		};
		array<glutton> x;
		for (int i=0;i<100;i++)
		{
			x.append();
		}
	}
	
	{
		array<int> x = { 3,1,4,5,9,2,6,8,7,0 };
		array<int> y = { 3,1,4,5,9,2,6,8,7,0 };
		array<int> z = { 0,1,2,3,4,5,6,7,8,9 };
		x.sort();
		y.sort([](int a, int b) { return a < b; });
		assert_eq(x, z);
		assert_eq(y, z);
	}
	
	{
		array<int> x = { 3,1,4,5,9,2,6,8,7,0 };
		array<int> y = { 3,1,4,5,9,2,6,8,7,0 };
		array<int> z = { 0,1,2,3,4,5,6,7,8,9 };
		x.ssort();
		y.ssort([](int a, int b) { return a < b; });
		assert_eq(x, z);
		assert_eq(y, z);
	}
	
	{
		array<int> x = { 1,2,3,4,5,6,7,8,9,0 };
		x = x.slice(3, 6);
		assert_eq(x.size(), 6);
		assert_eq(x[0], 4);
		assert_eq(x[1], 5);
		assert_eq(x[2], 6);
	}
	
	{
		array<int> x = { 0,1,2,3,4,5,6,7,8,9,0,1,2,3,4,5,6,7,8,9 };
		array<int> y = { 4,5,6,7,8,9,0,1,2,3,4,5 };
		x = x.slice(4, 12);
		assert_eq(x, y);
	}
	
	{
		array<int> x = { 0,1,2,3,4,5,6,7,8,9 };
		array<int> y = { 0,1,2,5,6,7,8,9 };
		x.remove_range(3, 5);
		assert_eq(x, y);
	}
	
	{
		array<int> x = { 1, 2, 3, 4, 5 };
		x.swap(1,3);
		assert_eq(tostring_dbg(x), "[1,4,3,2,5]");
		x.swap(3,1);
		assert_eq(tostring_dbg(x), "[1,2,3,4,5]");
	}
	
	{
		array<int> x = { 13,43,11,21,41,31,42,23,33,14,32,22,44,12,34,24 };
		array<int> y = { 13,11,14,12,21,23,22,24,31,33,32,34,43,41,42,44 };
		x.ssort([](const int& a, const int& b) { assert(&a > &b); return a/10 < b/10; });
		assert_eq(x, y);
	}
	
	{
		array<int> x = { 0,1,2,3,4,5,6,7,8,9 };
		int n_comp = 0;
		x.ssort([&](int a, int b) { n_comp++; return a<b; });
		assert_lte(n_comp, x.size());
	}
	
	{
		array<int> x;
		x.insert(0, 42);
		for (int i : range(50))
			x.insert(i, x[i]); // passes if Valgrind is happy
	}
	
	{
		array<int> n;
		n.resize(32);
		n = n.skip(32);
		assert_eq(n.size(), 0);
	}
	
	{
		array<int> a;
		a.resize(32);
		array<int> b;
		test_nomalloc {
			b = std::move(a);
		}
	}
	
	// just ensure this compiles
	{
		arrayview<int> a;
		arrayview<int> b(a);
		arrayview<int> c = a;
		c = a;
		arrayview<const int> d = a;
		arrayview<const int> e = d;
		arrayview<int> f = e;
		d = a;
		f = e;
	}
	
	{
		struct chunk { int a; int b; int c; };
		array<chunk> a;
		a.append({ 1, 2, 3 });
		a.append({ 1, 2 });
	}
	
	{
		array<string> a;
		a.append("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
		a.append("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
		a.append("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
		sarray<arrayview<string>,2> b = { a, a };
		assert_eq(b[0].size(), 3);
		assert_eq(b[1].size(), 3);
		assert_eq(b[0][0], "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
		assert_eq(b[0][1], "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
		assert_eq(b[0][2], "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
		assert_eq(b[1][0], "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
		assert_eq(b[1][1], "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
		assert_eq(b[1][2], "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
	}
	
	{
		array<int> a = { 1, 2, 3, 4, 5, 6, 7, 8 };
		array<int> b = { 10, 11, 12, 13 };
		a.insert_range(2, b);
		array<int> c = { 1, 2, 10, 11, 12, 13, 3, 4, 5, 6, 7, 8 };
		assert_eq(a, c);
		a.insert_range(6, a.slice(1, 2));
		array<int> d = { 1, 2, 10, 11, 12, 13, 2, 10, 3, 4, 5, 6, 7, 8 };
		assert_eq(a, d);
		a.insert_range(2, a.slice(4, 2));
		array<int> e = { 1, 2, 12, 13, 10, 11, 12, 13, 2, 10, 3, 4, 5, 6, 7, 8 };
		assert_eq(a, e);
		a.insert_range(4, a.slice(2, 4));
		array<int> f = { 1, 2, 12, 13, 12, 13, 10, 11, 10, 11, 12, 13, 2, 10, 3, 4, 5, 6, 7, 8 };
		assert_eq(a, f);
	}
}


static string ones_zeroes(int ones, int zeroes)
{
	string ret;
	for (int i=0;i<ones;i++) ret+="1";
	for (int i=0;i<zeroes;i++) ret+="0";
	return ret;
}
test("bitarray", "", "array")
{
	for (size_t up=0;up<256;up+=23)
	for (size_t down=0;down<=up;down+=23)
	{
		bitarray b;
		for (size_t i=0;i<up;i++)
		{
			assert_eq(b.size(), i);
			b.append(true);
		}
		assert_eq(b.size(), up);
		
		b.resize(down);
		assert_eq(tostring_dbg(b), ones_zeroes(down, 0));
		assert_eq(b.size(), down);
		for (size_t i=0;i<b.size();i++)
		{
			if (i<down) assert(b[i]);
			else assert(!b[i]);
		}
		assert_eq(b.size(), down);
		
		b.resize(up);
		assert_eq(tostring_dbg(b), ones_zeroes(down, up-down));
		assert_eq(b.size(), up);
		for (size_t i=0;i<b.size();i++)
		{
			if (i<down) assert(b[i]);
			else assert(!b[i]);
		}
	}
	
	{
		bitarray b;
		b.resize(8);
		assert_eq(tostring_dbg(array<size_t>::from(b.true_idxs())), "[]");
		b[0] = true;
		b[1] = false;
		b[2] = true;
		b[3] = true;
		b[4] = false;
		b[5] = true;
		b[6] = true;
		b[7] = true;
		assert_eq(tostring_dbg(b), "10110111");
		
		array<size_t> trues = array<size_t>::from(b.true_idxs());
		assert_eq(tostring_dbg(trues), "[0,2,3,5,6,7]");
		
		b.reset();
		b.resize(16);
		assert_eq(tostring_dbg(b), "0000000000000000");
	}
	
	{
		bitarray b;
		b.resize(65);
		b.resize(64);
		assert_eq(tostring_dbg(b), "0000000000000000000000000000000000000000000000000000000000000000");
		
		assert_eq(tostring_dbg(array<size_t>::from(b.true_idxs())), "[]");
		b[41] = true;
		assert_eq(tostring_dbg(array<size_t>::from(b.true_idxs())), "[41]");
	}
	
	{
		bitset<11> b;
		uint32_t bits_raw;
		for (int i=0;i<11;i++)
			b[i] = true;
		assert_eq(tostring_dbg(b), "11111111111");
		b = ~b;
		assert_eq(tostring_dbg(b), "00000000000");
		memcpy(&bits_raw, &b, sizeof(bits_raw));
		assert_eq(bits_raw, 0);
	}
	
	{
		bitarray a;
		a.resize(256);
		bitarray b;
		b.resize(128);
		a |= b;
		assert_eq(a.size(), 256);
		assert_eq(b.size(), 128);
		b |= a;
		assert_eq(a.size(), 256);
		assert_eq(b.size(), 256);
	}
	
	static_assert(sizeof(bitset<256>) == 256/8);
}

// these tests pass if they compile
inline arrayview<int> test_ret_arrayview()
{
	static const int y[4] = { 1, 2, 3, 4 };
	return y;
}
inline arrayvieww<int> test_ret_arrayvieww()
{
	static int y[4] = { 1, 2, 3, 4 };
	return y;
}
inline array<int> test_ret_array()
{
	int y[4] = { 1, 2, 3, 4 };
	return y;
}
#endif

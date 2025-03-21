#pragma once
#include "global.h"
#include "array.h"
#include "hash.h"
#include <string.h>

// define my own ctype, because table lookup is faster than libc call that probably ends up in a table lookup anyways,
//  and so I can define weird whitespace (\f\v) to not space (several Arlib modules require that, better centralize it)
// this means they don't obey locale, but all modern locales use UTF-8, for which isctype() has no useful answer
// locale shouldn't be in libc anyways; localization is complex enough to belong in a separate library that updates faster than libc,
//  and its global-state-based design interacts badly with libraries, logging, threading, text-based formats like JSON, etc

#ifdef _WIN32
#include <ctype.h> // include this one before windows.h does, the defines below confuse it
#endif
#define iscntrl my_iscntrl // define them all away, then don't bother implementing the ones I don't use
#define isprint my_isprint
#define isspace my_isspace
#define isblank my_isblank
#define isgraph my_isgraph
#define ispunct my_ispunct
#define isalnum my_isalnum
#define isalpha my_isalpha
#define isupper my_isupper
#define islower my_islower
#define isdigit my_isdigit
#define isxdigit my_isxdigit
#define tolower my_tolower
#define toupper my_toupper

extern const uint8_t char_props[256];
// bit meanings:
// 0x80 - space (\t\n\r )             tolower/toupper needs 0x20 to be letter,
// 0x40 - C space (\t\n\v\f\r )         and 0x80 is cheaper to test on some platforms, so it goes to the most common test (space)
// 0x20 - letter (A-Za-z)               other bit assignments are arbitrary
// 0x10 - alphanumeric (0-9A-Za-z)    contrary to libc, these functions handle byte values only;
// 0x08 - hex digit (0-9A-Fa-f)         EOF is not a valid input (EOF feels like a poor design)
// 0x04 - unused
// 0x02 - unused
// 0x01 - unused
static forceinline bool isspace(uint8_t c) { return char_props[c] & 0x80; }
static forceinline bool iscspace(uint8_t c) { return char_props[c] & 0x40; }
static forceinline bool isdigit(uint8_t c) { return c >= '0' && c <= '9'; } // range check is cheaper than table lookup and bit check
static forceinline bool isalpha(uint8_t c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); } // optimized to bitand plus range check
static forceinline bool islower(uint8_t c) { return c >= 'a' && c <= 'z'; }
static forceinline bool isupper(uint8_t c) { return c >= 'A' && c <= 'Z'; }
static forceinline bool isalnum(uint8_t c) { return char_props[c] & 0x10; } // multi range check is expensive
static forceinline bool isxdigit(uint8_t c) { return char_props[c] & 0x08; }
static forceinline uint8_t tolower(uint8_t c) { return c|(char_props[c]&0x20); }
static forceinline uint8_t toupper(uint8_t c) { return c&~(char_props[c]&0x20); }



// A string is a mutable byte sequence. It usually represents UTF-8 text, but can be arbitrary binary data, including NULs.
// All string functions taking or returning a char* assume/guarantee NUL termination. Anything using uint8_t* does not.

// cstring is an immutable sequence of bytes that does not own its storage; it usually points to a string constant, or part of a string.
// In most contexts, it's called stringview, but I feel that's too long.
// Long ago, cstring was just a typedef to 'const string&', hence its name.

// The child classes put various constraints on their contents that the parent does not; they do not obey the Liskov substitution principle.
// Any attempt to turn a string into cstring&, then call operator= or otherwise mutate it, is object slicing and undefined behavior.

class cstring;
class cstrnul;
class string;

class regex;

#define OBJ_SIZE 16 // maximum 120, or the inline length overflows
                    // (127 would fit, but that requires an extra alignment byte, which throws the sizeof assert)
                    // minimum 16 on 64bit, 12 on 32bit
                    // most strings are short, so let's keep it small; 16 for all
#define MAX_INLINE (OBJ_SIZE-1) // macros instead of static const to make gdb not print them every time

class cstring {
	friend class string;
	friend class cstrnul;
	friend bool operator==(const cstring& left, const cstring& right);
	friend inline bool operator==(const cstring& left, const char * right);
	
	static uint32_t max_inline() { return MAX_INLINE; }
	
	union {
		struct {
			uint8_t m_inline[MAX_INLINE+1];
			// last byte is how many bytes are unused by the raw string data
			// if all bytes are used, there are zero unused bytes - which also serves as the NUL
			// if not inlined, it's -1
			// unused bytes in m_inline (beyond NUL terminator) are always 00
		};
		struct {
			uint8_t* m_data;
			uint32_t m_len; // always > MAX_INLINE, if not inlined; some of the operator==s demand that
			bool m_nul; // whether the string is properly terminated (always true for string, possibly false for cstring)
			// 2 unused bytes here
			uint8_t m_reserved; // reserve space for the last byte of the inline data; never ever access this
		};
	};
	
	uint8_t& inline_len_encoded_w() { return m_inline[MAX_INLINE]; }
	int8_t inline_len_encoded() const { return m_inline[MAX_INLINE]; }
	
	forceinline bool inlined() const
	{
		static_assert(sizeof(cstring)==OBJ_SIZE);
		return inline_len_encoded() >= 0;
	}
	
	forceinline uint8_t len_if_inline() const
	{
		static_assert((MAX_INLINE & (MAX_INLINE+1)) == 0); // this xor trick only works for power of two minus 1
		return MAX_INLINE^inline_len_encoded();
	}
	
	forceinline const uint8_t * ptr() const
	{
		if (inlined()) return m_inline;
		else return m_data;
	}
	
	forceinline bytesw bytes_raw() const
	{
		if (inlined())
			return bytesw((uint8_t*)m_inline, len_if_inline());
		else
			return bytesw(m_data, m_len);
	}
	
public:
	forceinline const char * ptr_raw() const
	{
		return (char*)ptr();
	}
	forceinline const char * ptr_raw_end() const
	{
		return (char*)ptr() + length();
	}
	forceinline uint32_t length() const
	{
		if (inlined()) return len_if_inline();
		else return m_len;
	}
	
	forceinline bytesr bytes() const { return bytes_raw(); }
	//If this is true, bytes()[bytes().size()] is '\0'. If false, it's undefined behavior.
	//this[this->length()] is always '\0', even if this is false.
	forceinline bool bytes_hasterm() const
	{
		return (inlined() || m_nul);
	}
	
private:
	forceinline void init_empty()
	{
		memset(m_inline, 0, sizeof(m_inline));
		inline_len_encoded_w() = MAX_INLINE;
	}
	void init_from_nocopy(const char * str)
	{
		init_from_nocopy((uint8_t*)str, strlen(str), true);
	}
	void init_from_nocopy(const uint8_t * str, size_t len, bool has_nul = false)
	{
		if (len <= MAX_INLINE)
		{
			memset(m_inline, 0, sizeof(m_inline));
			for (uint32_t i=0;i<len;i++) m_inline[i] = str[i]; // memcpy's constant overhead is huge if len is unknown
			inline_len_encoded_w() = MAX_INLINE-len;
		}
		else
		{
			if (len > 0xFFFFFFFF) abort();
			inline_len_encoded_w() = -1;
			
			m_data = (uint8_t*)str;
			m_len = len;
			m_nul = has_nul;
		}
	}
	void init_from_nocopy(bytesr data, bool has_nul = false) { init_from_nocopy(data.ptr(), data.size(), has_nul); }
	void init_from_nocopy(const cstring& other) { *this = other; }
	
	// TODO: make some of these ctors constexpr, so gcc can optimize them into the data section (Clang already does)
	// partial or no initialization is c++20 only, so not until then
	// may need removing the union and memcpying things to/from a struct
	class noinit {};
	cstring(noinit) {}
	
	static bool equal_large(const cstring& left, const cstring& right);
	
public:
	cstring() { init_empty(); }
	cstring(const cstring& other) = default;
	//cstring(string&&) = delete; // causes trouble with cstring ctor in function argument position. operator= catches most trouble anyways
	
	cstring(const char * str) { init_from_nocopy(str); }
	cstring(const char8_t * str) { init_from_nocopy((char*)str); }
	cstring(bytesr bytes) { init_from_nocopy(bytes); }
	cstring(arrayview<char> chars) { init_from_nocopy(chars.transmute<uint8_t>()); }
	cstring(nullptr_t) { init_empty(); }
	// If has_nul, then bytes[bytes.size()] is zero. (Undefined behavior does not count as zero.)
	cstring(bytesr bytes, bool has_nul) { init_from_nocopy(bytes, has_nul); }
	cstring& operator=(const cstring& other) = default;
	cstring& operator=(bytesr bytes) { init_from_nocopy(bytes); return *this; }
	cstring& operator=(string&&) = delete;
	cstring& operator=(const char * str) { init_from_nocopy(str); return *this; }
	cstring& operator=(const char8_t * str) { init_from_nocopy((char*)str); return *this; }
	cstring& operator=(nullptr_t) { init_empty(); return *this; }
	
	explicit operator bool() const { return length() != 0; }
	
	forceinline uint8_t operator[](ssize_t index) const { return ptr()[index]; }
	
	//~0 means end of the string, ~1 is last character
	//don't try to make -1 the last character, it breaks str.substr(x, ~0)
	//this shorthand exists only for substr()
	int32_t realpos(int32_t pos) const
	{
		if (pos >= 0) return pos;
		else return length()-~pos;
	}
	cstring substr(int32_t start, int32_t end) const
	{
		start = realpos(start);
		end = realpos(end);
		return cstring(bytesr(ptr()+start, end-start), (bytes_hasterm() && (uint32_t)end == length()));
	}
	
	bool contains(cstring other) const
	{
		return memmem(this->ptr(), this->length(), other.ptr(), other.length()) != NULL;
	}
	size_t indexof(cstring other, size_t start = 0) const; // Returns -1 if not found.
	size_t lastindexof(cstring other) const;
	bool startswith(cstring other) const;
	bool endswith(cstring other) const;
	
	size_t iindexof(cstring other, size_t start = 0) const;
	size_t ilastindexof(cstring other) const;
	bool icontains(cstring other) const;
	bool istartswith(cstring other) const;
	bool iendswith(cstring other) const;
	bool iequals(cstring other) const;
	
	string replace(cstring in, cstring out) const;
	
	//fcrsplitwi - fixed-size cstring-returning backwards-counting split on word boundaries, inclusive
	//fixed-size - returns sarray, not array
	//cstring-returning - obvious
	//backwards-counting - splits at the rightmost opportunity, "a b c d".rsplit<1>(" ") is ["a b c", "d"]
	//word boundary - isspace()
	//inclusive - the boundary string is included in the output, "a\nb\n".spliti("\n") is ["a\n", "b\n"]
	//all subsets of splitting options are supported
	
	// todo:
	// - rename all the non-f splits to d (dynamic-size)
	// - change most callers to f
	// - remove f from most of them, sarray should be the default
	
	array<cstring> csplit(cstring sep, size_t limit) const;
	template<size_t limit = SIZE_MAX>
	array<cstring> csplit(cstring sep) const { return csplit(sep, limit); }
	
	array<cstring> crsplit(cstring sep, size_t limit) const;
	template<size_t limit>
	array<cstring> crsplit(cstring sep) const { return crsplit(sep, limit); }
	
	array<string> split(cstring sep, size_t limit) const { return csplit(sep, limit).transform<string>(); }
	template<size_t limit = SIZE_MAX>
	array<string> split(cstring sep) const { return split(sep, limit); }
	
	array<string> rsplit(cstring sep, size_t limit) const { return crsplit(sep, limit).transform<string>(); }
	template<size_t limit>
	array<string> rsplit(cstring sep) const { return rsplit(sep, limit); }
	
	
	array<cstring> cspliti(cstring sep, size_t limit) const;
	template<size_t limit = SIZE_MAX>
	array<cstring> cspliti(cstring sep) const { return cspliti(sep, limit); }
	
	array<cstring> crspliti(cstring sep, size_t limit) const;
	template<size_t limit>
	array<cstring> crspliti(cstring sep) const { return crspliti(sep, limit); }
	
	array<string> spliti(cstring sep, size_t limit) const { return cspliti(sep, limit).transform<string>(); }
	template<size_t limit = SIZE_MAX>
	array<string> spliti(cstring sep) const { return spliti(sep, limit); }
	
	array<string> rspliti(cstring sep, size_t limit) const { return crspliti(sep, limit).transform<string>(); }
	template<size_t limit>
	array<string> rspliti(cstring sep) const { return rspliti(sep, limit); }
	
	
private:
	void csplit_to(cstring sep, arrayvieww<cstring> target) const;
	void crsplit_to(cstring sep, arrayvieww<cstring> target) const;
	void cspliti_to(cstring sep, arrayvieww<cstring> target) const;
	void crspliti_to(cstring sep, arrayvieww<cstring> target) const;
public:
	template<size_t limit>
	sarray<cstring,limit+1> fcsplit(cstring sep) const { sarray<cstring,limit+1> ret; csplit_to(sep, ret); return ret; }
	template<size_t limit>
	sarray<cstring,limit+1> fcrsplit(cstring sep) const { sarray<cstring,limit+1> ret; crsplit_to(sep, ret); return ret; }
	template<size_t limit>
	sarray<string,limit+1> fsplit(cstring sep) const { return fcsplit<limit>(sep).template transform<string>(); }
	template<size_t limit>
	sarray<string,limit+1> frsplit(cstring sep) const { return fcrsplit<limit>(sep).template transform<string>(); }
	
	template<size_t limit>
	sarray<cstring,limit+1> fcspliti(cstring sep) const { sarray<cstring,limit+1> ret; cspliti_to(sep, ret); return ret; }
	template<size_t limit>
	sarray<cstring,limit+1> fcrspliti(cstring sep) const { sarray<cstring,limit+1> ret; crspliti_to(sep, ret); return ret; }
	template<size_t limit>
	sarray<string,limit+1> fspliti(cstring sep) const { return fcspliti<limit>(sep).template transform<string>(); }
	template<size_t limit>
	sarray<string,limit+1> frspliti(cstring sep) const { return fcrspliti<limit>(sep).template transform<string>(); }
	
	array<cstring> csplit(const regex& rx, size_t limit) const;
	template<size_t limit = SIZE_MAX>
	array<cstring> csplit(const regex& rx) const { return csplit(rx, limit); }
	
	array<string> split(const regex& rx, size_t limit) const
	{
		return csplit(rx, limit).template transform<string>();
	}
	template<size_t limit = SIZE_MAX, typename T>
	array<string> split(const regex& rx) const { return split(rx, limit); }
	
	inline string upper() const; // Only considers ASCII, will not change ø. Will capitalize a decomposed ñ, but not a precomposed one.
	inline string lower() const;
	cstring trim() const; // Deletes whitespace at start and end. Does not do anything to consecutive whitespace in the middle.
	bool contains_nul() const;
	
	bool isutf8() const; // NUL is considered valid UTF-8. Modified UTF-8, CESU-8, WTF-8, etc are not.
	// The index is updated to point to the next codepoint. Initialize it to zero; stop when it equals the string's length.
	// If invalid UTF-8, or descynchronized index, returns U+DC80 through U+DCFF; callers are welcome to treat this as an error.
	uint32_t codepoint_at(uint32_t& index) const;
	
	//Whether the string matches a glob pattern. ? in 'pat' matches any byte (not utf8 codepoint), * matches zero or more bytes.
	//NUL bytes are treated as any other byte, in both strings.
	bool matches_glob(cstring pat) const __attribute__((pure)) { return matches_glob(pat, false); }
	// Case insensitive. Considers ASCII only, øØ are considered nonequal.
	bool matches_globi(cstring pat) const __attribute__((pure)) { return matches_glob(pat, true); }
private:
	bool matches_glob(cstring pat, bool case_insensitive) const __attribute__((pure));
public:
	cstring strip() const;
	
	string leftPad (size_t len, uint8_t ch = ' ') const;
	
	size_t hash() const { return ::hash(ptr(), length()); }
	
private:
	class c_string {
		char* ptr;
		bool do_free;
	public:
		
		c_string(bytesr data, bool has_term)
		{
			if (has_term)
			{
				ptr = (char*)data.ptr();
				do_free = false;
			}
			else
			{
				ptr = (char*)xmalloc(data.size()+1);
				memcpy(ptr, data.ptr(), data.size());
				ptr[data.size()] = '\0';
				do_free = true;
			}
		}
		operator const char *() const { return ptr; }
		const char * c_str() const { return ptr; }
		inline operator cstrnul() const;
		~c_string() { if (do_free) free(ptr); }
	};
public:
	//no operator const char *, a cstring doesn't necessarily have a NUL terminator
	c_string c_str() const { return c_string(bytes(), bytes_hasterm()); }
	
	friend forceinline bool operator==(const cstring& left, const char * right)
	{
		size_t len = strlen(right);
		if (__builtin_constant_p(len))
		{
			if (len <= cstring::max_inline())
			{
				cstring tmp = right; // optimizes poorly, but...
				return left == tmp;
			}
			else
				return (!left.inlined() && left.m_len == len && memeq(left.m_data, right, len));
		}
		else return left.bytes() == bytesr((uint8_t*)right, len);
	}
	
	friend inline bool operator==(const char * left, const cstring& right) { return operator==(right, left); }
	friend inline bool operator==(const cstring& left, const cstring& right)
	{
		if (left.inlined())
			return (memeq(&left, &right, sizeof(cstring)));
		else
			return cstring::equal_large(left, right);
	}
	friend inline bool operator!=(const cstring& left, const char * right  ) { return !operator==(left, right); }
	friend inline bool operator!=(const cstring& left, const cstring& right) { return !operator==(left, right); }
	friend inline bool operator!=(const char * left,   const cstring& right) { return !operator==(left, right); }
	
	friend bool operator<(cstring left,      const char * right) = delete;
	friend bool operator<(cstring left,      cstring right     ) = delete;
	friend bool operator<(const char * left, cstring right     ) = delete;
};


// Like cstring, but guaranteed to have a nul terminator.
class cstrnul : public cstring {
	friend class cstring;
	friend class string;
	forceinline const char * ptr_withnul() const { return (char*)ptr(); }
	
	class has_nul {};
	cstrnul(noinit) : cstring(noinit()) {}
	cstrnul(bytesr bytes, has_nul) : cstring(noinit()) { init_from_nocopy(bytes, true); }
	
public:
	cstrnul() { init_empty(); }
	cstrnul(const cstrnul& other) = default;
	cstrnul(const char * str) { init_from_nocopy(str); }
	
	cstrnul(nullptr_t) { init_empty(); }
	cstrnul& operator=(const cstring& other) = delete;
	cstrnul& operator=(const cstrnul& other) = default;
	cstrnul& operator=(const char * str) { init_from_nocopy(str); return *this; }
	cstrnul& operator=(nullptr_t) { init_empty(); return *this; }
	
	explicit operator bool() const { return length() != 0; }
	operator const char * () const { return ptr_withnul(); }
	
	cstrnul substr_nul(int32_t start) const
	{
		start = realpos(start);
		return cstrnul(bytesr(ptr()+start, length()-start), has_nul());
	}
};


class string : public cstrnul {
	friend class cstring;
	friend class cstrnul;
	
	static size_t bytes_for(size_t len) { return bitround(len+1); }
	forceinline uint8_t * ptr() { return (uint8_t*)cstring::ptr(); }
	forceinline const uint8_t * ptr() const { return cstring::ptr(); }
	void resize(size_t newlen);
	
	void init_from(const char * str)
	{
		//if (!str) str = "";
		init_from((uint8_t*)str, strlen(str));
	}
	forceinline void init_from(const uint8_t * str, size_t len)
	{
		if (__builtin_constant_p(len))
		{
			if (len <= MAX_INLINE)
			{
				memset(m_inline, 0, sizeof(m_inline));
				memcpy(m_inline, str, len);
				inline_len_encoded_w() = max_inline()-len;
			}
			else init_from_large(str, len);
		}
		else init_from_outline(str, len);
	}
	forceinline void init_from(bytesr data) { init_from(data.ptr(), data.size()); }
	void init_from_outline(const uint8_t * str, size_t len);
	void init_from_large(const uint8_t * str, size_t len);
	void init_from(const cstring& other);
	void init_from(string&& other)
	{
		memcpy((void*)this, (void*)&other, sizeof(*this));
		other.init_empty();
	}
	
	void reinit_from(const char * str)
	{
		if (!str) str = "";
		reinit_from(bytesr((uint8_t*)str, strlen(str)));
	}
	void reinit_from(bytesr data);
	void reinit_from(cstring other)
	{
		reinit_from(other.bytes());
	}
	void reinit_from(string&& other)
	{
		deinit();
		memcpy((void*)this, (void*)&other, sizeof(*this));
		other.init_empty();
	}
	
	void deinit()
	{
		if (!inlined()) free(m_data);
	}
	
	void append(bytesr newdat);
	
	void append(uint8_t newch)
	{
		uint32_t oldlength = length();
		resize(oldlength + 1);
		ptr()[oldlength] = newch;
	}
	
public:
	//Resizes the string to a suitable size, then allows the caller to fill it in. Initial contents are undefined.
	bytesw construct(size_t len)
	{
		resize(len);
		return bytes();
	}
	
	string& operator+=(const char * right)
	{
		append(bytesr((uint8_t*)right, strlen(right)));
		return *this;
	}
	
	string& operator+=(cstring right)
	{
		append(right.bytes());
		return *this;
	}
	
	
	string& operator+=(char right)
	{
		append((uint8_t)right);
		return *this;
	}
	
	string& operator+=(uint8_t right)
	{
		append(right);
		return *this;
	}
	
	// for other integer types, fail (short/long/etc will be ambiguous)
	string& operator+=(int right) = delete;
	string& operator+=(unsigned right) = delete;
	
	
	string() : cstrnul(noinit()) { init_empty(); }
	string(const string& other) : cstrnul(noinit()) { init_from(other); }
	string(string&& other) : cstrnul(noinit()) { init_from(std::move(other)); }
	
	forceinline string(cstring other) : cstrnul(noinit()) { init_from(other); }
	forceinline string(bytesr bytes) : cstrnul(noinit()) { init_from(bytes); }
	forceinline string(arrayview<char> chars) : cstrnul(noinit()) { init_from(chars.transmute<uint8_t>()); }
	forceinline string(const char * str) : cstrnul(noinit()) { init_from(str); }
	forceinline string(const char8_t * str) : cstrnul(noinit()) { init_from((char*)str); }
	string(array<uint8_t>&& bytes);
	string(nullptr_t) = delete;
	forceinline string& operator=(const string& other) { reinit_from(other); return *this; }
	forceinline string& operator=(const cstring& other) { reinit_from(other); return *this; }
	forceinline string& operator=(string&& other) { reinit_from(std::move(other)); return *this; }
	forceinline string& operator=(const char * str) { reinit_from(str); return *this; }
	forceinline string& operator=(const char8_t * str) { reinit_from((char*)str); return *this; }
	forceinline string& operator=(nullptr_t) { deinit(); init_empty(); return *this; }
	~string() { deinit(); }
	
	explicit operator bool() const { return length() != 0; }
	operator const char * () const { return ptr_withnul(); }
	
	//Reading the NUL terminator is fine. Writing the terminator, or poking beyond the NUL, is undefined behavior.
	forceinline uint8_t& operator[](int index) { return ptr()[index]; }
	forceinline uint8_t operator[](int index) const { return ptr()[index]; }
	
	forceinline bytesr bytes() const { return bytes_raw(); }
	forceinline bytesw bytes() { return bytes_raw(); }
	
	//Takes ownership of the given pointer. Will free() it when done.
	static string create_usurp(char * str);
	static string create_usurp(array<uint8_t>&& in) { return string(std::move(in)); }
	
	//Returns a string containing a single NUL.
	static cstring nul() { return bytesr((uint8_t*)"", 1); }
	
	//Returns U+FFFD for UTF16-reserved codepoints and other forbidden codepoints. 0 yields a NUL byte.
	static string codepoint(uint32_t cp);
	// Returns number of bytes written. Buffer must be at least 4 bytes. Does not NUL terminate.
	// May write garbage between out+return and out+4.
	static size_t codepoint(uint8_t* out, uint32_t cp);
	
	string upper() const & { return string(*this).upper(); }
	string upper() && // Only considers ASCII, will not change ø. Will capitalize a decomposed ñ, but not a precomposed one.
	{
		bytesw by = this->bytes();
		for (size_t i=0;i<by.size();i++) by[i] = toupper(by[i]);
		return std::move(*this);
	}
	
	string lower() const & { return string(*this).lower(); }
	string lower() &&
	{
		bytesw by = this->bytes();
		for (size_t i=0;i<by.size();i++) by[i] = tolower(by[i]);
		return std::move(*this);
	}
	
	//3-way comparison. If a comes first, return value is negative; if equal, zero; if b comes first, positive.
	//Comparison is bytewise. End goes before NUL, so the empty string comes before everything else.
	//The return value is not guaranteed to be in [-1..1]. It's not even guaranteed to fit in anything smaller than int.
	static int compare3(cstring a, cstring b);
	//Like the above, but case insensitive (treat every letter as uppercase). Considers ASCII only, øØ are considered nonequal.
	//If the strings are case-insensitively equal, uppercase goes first.
	static int icompare3(cstring a, cstring b);
	static bool less(cstring a, cstring b) { return compare3(a, b) < 0; }
	static bool iless(cstring a, cstring b) { return icompare3(a, b) < 0; }
	
	//Natural comparison; "8" < "10". Other than that, same as above.
	//Exact rules:
	//  Strings are compared component by component. A component is either a digit sequence, or a non-digit. 8 < 10, 2 = 02
	//  - and . are not part of the digit sequence. -1 < -2, 1.2 < 1.03
	//  If the strings are otherwise equal, repeat the comparison, but with 2 < 02. If still equal, repeat case sensitively (if applicable).
	//  Digits (0x30-0x39) belong after $ (0x24), but before @ (0x40).
	//Correct sorting is a$ a1 a2 a02 a2a a2a1 a02a2 a2a3 a2b a02b A3A A3a a3A a3a A03A A03a a03A a03a a10 a11 aa a@
	//It's named snat... instead of nat... because case sensitive natural comparison is probably a mistake; it shouldn't be the default.
	static int snatcompare3(cstring a, cstring b) { return string::natcompare3(a, b, false); }
	static int inatcompare3(cstring a, cstring b) { return string::natcompare3(a, b, true); }
	static bool snatless(cstring a, cstring b) { return snatcompare3(a, b) < 0; }
	static bool inatless(cstring a, cstring b) { return inatcompare3(a, b) < 0; }
private:
	static int natcompare3(cstring a, cstring b, bool case_insensitive);
public:
};
cstring::c_string::operator cstrnul() const { return ptr; }

inline string cstring::upper() const { return string(*this).lower(); }
inline string cstring::lower() const { return string(*this).lower(); }

#undef OBJ_SIZE
#undef MAX_INLINE


inline string operator+(cstring left,      cstring right     ) { string ret = left; ret += right; return ret; }
inline string operator+(cstring left,      const char * right) { string ret = left; ret += right; return ret; }
inline string operator+(string&& left,     cstring right     ) { left += right; return left; }
inline string operator+(string&& left,     const char * right) { left += right; return left; }
inline string operator+(const char * left, cstring right     ) { string ret = left; ret += right; return ret; }

inline string operator+(string&& left, char right) = delete;
inline string operator+(cstring left, char right) = delete;
inline string operator+(char left, cstring right) = delete;

template<typename T>
cstring arrayview<T>::get_or(size_t n, const char * def) const
{
	if (n < count) return items[n];
	else return def;
};

class smelly_string {
	array<uint16_t> body;
public:
#ifdef _WIN32
	smelly_string() {}
	smelly_string(cstring utf8) : body(utf8_to_utf16(utf8)) { body.append('\0'); }
	
	uint16_t* resize(size_t len) { body.resize(len+1); body[len] = '\0'; return body.ptr(); }
	
	operator const uint16_t*() const { return body.ptr(); }
	operator const wchar_t*() const { return (wchar_t*)body.ptr(); }
	
	operator string() const { return utf16_to_utf8(body.slice(0, body.size()-1)); }
#endif
	
	static string ucs1_to_utf8(arrayview<uint8_t> ucs1);
	static string utf16_to_utf8(arrayview<uint16_t> utf16);
	static string utf16l_to_utf8(arrayview<uint8_t> utf16);
	
	// Latter's size() must be at least 3x former. Returns number of bytes actually written.
	static size_t utf16_to_utf8_buf(arrayview<uint16_t> utf16, arrayvieww<uint8_t> utf8);
	
#ifdef _WIN32
	// this specific function is so smelly I won't even bother implementing it on linux
	static array<uint16_t> utf8_to_utf16(cstring utf8);
	
	// and these are nonportable
	static_assert(sizeof(wchar_t) == sizeof(uint16_t));
	static string utf16_to_utf8(arrayview<wchar_t> utf16) { return utf16_to_utf8(utf16.transmute<uint16_t>()); }
	static size_t utf16_to_utf8_buf(arrayview<wchar_t> utf16, arrayvieww<uint8_t> utf8)
		{ return utf16_to_utf8_buf(utf16.transmute<uint16_t>(), utf8); }
#endif
};

template<bool invert, typename T, size_t n>
class char_set {
	static constexpr bitset<256> compile_charset()
	{
		bitset<256> ret;
		const char * str = T()();
		for (size_t i=0;i<n;i++)
			ret[(uint8_t)str[i]] = true;
		if (invert)
			ret = ~ret;
		return ret;
	}
	static constexpr bitset<256> bits = compile_charset();
public:
	bool contains(uint8_t chr) const { return bits[chr]; }
	operator const bitset<256>&() const { return bits; }
	auto operator~() const { return char_set<!invert, T, n>(); }
};
template<size_t n, typename T>
static auto compile_charset(T get_str)
{
	return char_set<false, T, n>();
}
// needs a bit of extra trickery, can't just decltype(lambda), it confuses gcc (compiler bug I think)
#define CHAR_SET(str) (compile_charset<sizeof(str)-1>([]{ return "" str ""; }))

#ifndef puts
#ifdef _WIN32
void puts(cstring str);
#else
inline void puts(cstring str) { fwrite(str.ptr_raw(), 1, str.length(), stdout); fputc('\n', stdout); }
#endif
#endif

#pragma once
#include "global.h"
#include "array.h"
#include "hash.h"
#include "simd.h"
#include"os.h"
#include <string.h>

// define my own ctype, because table lookup is faster than libc call that probably ends up in a table lookup anyways,
//  and so I can define weird whitespace (\f \v) to not space (several Arlib modules require that, better centralize it)
// this means they don't obey locale, but all modern locales use UTF-8, for which isctype() has no useful answer
// locale shouldn't be in libc anyways; localization is complex enough to belong in a separate library that updates faster than libc,
//  and its global state based design interacts badly with libraries, logging, threading, text-based formats like JSON, etc

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
// 0x01 - hex digit (0-9A-Fa-f)
// 0x02 - uppercase (A-Z)
// 0x04 - lowercase (a-z)
// 0x08 - unused (testing &0x60 is as cheap as &0x08, and I'm not sure what else would be common enough to spend these bits on)
// 0x10 - unused (best I can think of would be ispunct or underscore, but they'd have only one or two callers each across my projects)
// 0x20 - letter (A-Za-z - 20 rather than 06 to optimize tolower and toupper. Can be removed if I need another bit in the table.)
// 0x40 - digit (0-9)
// 0x80 - space (\t\n\r ) (0x80 bit is the cheapest to test, and isspace is the most common isctype())
inline bool isspace(uint8_t c) { return char_props[c] & 0x80; } // C standard says \f \v are space, but this one disagrees
inline bool isdigit(uint8_t c) { return char_props[c] & 0x40; }
inline bool isalpha(uint8_t c) { return char_props[c] & 0x20; } // C standard also says they must give sensible answers for EOF,
inline bool islower(uint8_t c) { return char_props[c] & 0x04; } //  but I don't use EOF, so I'll stick to the 256 byte values only
inline bool isupper(uint8_t c) { return char_props[c] & 0x02; }
inline bool isalnum(uint8_t c) { return char_props[c] & 0x60; }
inline bool isxdigit(uint8_t c) { return char_props[c] & 0x01; }
inline uint8_t tolower(uint8_t c) { return c|(char_props[c]&0x20); }
inline uint8_t toupper(uint8_t c) { return c&~(char_props[c]&0x20); }



// A string is a mutable byte container. It usually represents UTF-8 text, but can be arbitrary binary data, including NULs.
// All string functions taking or returning a char* assume/guarantee NUL termination. Anything using uint8_t* does not.

// cstring is an immutable sequence of bytes that does not own its storage; it usually points to a string constant, or part of a string.
// In most contexts, it's called stringview, but I feel that's too long.
// Long ago, cstring was just a typedef to 'const string&', hence its name.

class string;

#define OBJ_SIZE 16 // maximum 120, or the inline length overflows
                    // (127 would fit, but that requires an extra alignment byte, which throws the sizeof assert)
                    // minimum 16 on 64bit, 12 on 32bit
                    // most strings are short, so let's keep it small; 16 for all
#define MAX_INLINE (OBJ_SIZE-1) // macros instead of static const to make gdb not print them every time

class cstring {
	friend class string;
	friend inline bool operator==(const cstring& left, const cstring& right);
#if __GNUC__ == 7
	template<size_t N> friend inline bool operator==(const cstring& left, const char (&right)[N]);
#else
	friend inline bool operator==(const cstring& left, const char * right);
#endif
	
	static uint32_t max_inline() { return MAX_INLINE; }
	
	union {
		struct {
			uint8_t m_inline[MAX_INLINE+1];
			// last byte is how many bytes are unused by the raw string data
			// if all bytes are used, there are zero unused bytes - which also serves as the NUL
			// if not inlined, it's -1
		};
		struct {
			uint8_t* m_data;
			uint32_t m_len; // always > OBJ_SIZE, if not inlined; some of the operator== demand that
			bool m_nul; // whether the string is properly terminated (always true for string, possibly false for cstring)
			// 2 unused bytes here
			uint8_t m_reserved; // reserve space for the last byte of the inline data; never ever access this
		};
	};
	
	uint8_t& m_inline_len_w() { return m_inline[MAX_INLINE]; }
	int8_t m_inline_len() const { return m_inline[MAX_INLINE]; }
	
	forceinline bool inlined() const
	{
		static_assert(sizeof(cstring)==OBJ_SIZE);
		return m_inline_len() >= 0;
	}
	
	forceinline uint8_t len_if_inline() const
	{
		static_assert((MAX_INLINE & (MAX_INLINE+1)) == 0); // this xor trick only works for power of two minus 1
		return MAX_INLINE^m_inline_len();
	}
	
	forceinline const uint8_t * ptr() const
	{
		if (inlined()) return m_inline;
		else return m_data;
	}
	
	forceinline arrayvieww<uint8_t> bytes_raw() const
	{
		if (inlined())
			return arrayvieww<uint8_t>((uint8_t*)m_inline, len_if_inline());
		else
			return arrayvieww<uint8_t>(m_data, m_len);
	}
	
public:
	forceinline uint32_t length() const
	{
		if (inlined()) return len_if_inline();
		else return m_len;
	}
	
	forceinline arrayview<uint8_t> bytes() const { return bytes_raw(); }
	//If this is true, bytes()[bytes().size()] is '\0'. If false, it's undefined behavior.
	//this[this->length()] is always '\0', even if this is false.
	forceinline bool bytes_hasterm() const
	{
		return (inlined() || m_nul);
	}
	
private:
	forceinline void init_empty()
	{
		m_inline_len_w() = MAX_INLINE;
		m_inline[0] = '\0';
	}
	void init_from_nocopy(const char * str)
	{
		// TODO: delete at dec5 if no issues found before then
		if (!str) debug_warn_stack("init_from_nocopy(null)");
		if (!str) str = "";
		init_from_nocopy(arrayview<uint8_t>((uint8_t*)str, strlen(str)), true);
	}
	void init_from_nocopy(const uint8_t * str, uint32_t len, bool has_nul = false)
	{
		if (len <= MAX_INLINE)
		{
			for (uint32_t i=0;i<len;i++) m_inline[i] = str[i]; // memcpy's constant overhead is huge
			m_inline[len] = '\0';
			m_inline_len_w() = MAX_INLINE-len;
		}
		else
		{
			m_inline_len_w() = -1;
			
			m_data = (uint8_t*)str;
			m_len = len;
			m_nul = has_nul;
		}
	}
	void init_from_nocopy(arrayview<uint8_t> data, bool has_nul = false) { init_from_nocopy(data.ptr(), data.size(), has_nul); }
	void init_from_nocopy(const cstring& other) { *this = other; }
	
	char getchar(uint32_t index) const
	{
// TODO: I want to delete the 'cstring[cstring.length()] is always nul' guarantee
// check added oct 7; at least one buggy caller existed on nov 20, keep check until at least dec 20
if(index==length())debug_warn_stack("cstring::getchar() bad index\n");
		//this function is REALLY hot, use the strongest possible optimizations
		if (inlined()) return m_inline[index];
		else if (index < m_len) return m_data[index];
		else return '\0'; // for cstring, which isn't necessarily NUL-terminated
	}
	
	class noinit {};
	cstring(noinit) {}
	
public:
	cstring() { init_empty(); }
	cstring(const cstring& other) = default;
	
#if __GNUC__ == 7
	// disgusting hack to help gcc optimize string literal arguments properly
	// gcc 8+ optimizes properly, no need for this
	template<size_t N> void init_from_nocopy_literal(const char (&str)[N])
	{
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 8 && __GNUC__ <= 10 && __cplusplus >= 201103
		// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=91212
		// (dead code, but will keep as a reminder that this bug exists)
		init_from_nocopy((uint8_t*)str, strlen(str), true);
#else
		init_from_nocopy((uint8_t*)str, N-1, str[N-1]=='\0');
#endif
	}
	
	template<typename T, typename Ttest = std::enable_if_t<std::is_same_v<T,const char*> || std::is_same_v<T,char*>>>
	cstring(T str) { init_from_nocopy(str); }
	template<size_t N> cstring(const char (&str)[N]) { init_from_nocopy_literal<N>(str); }
	template<size_t N> cstring(char (&str)[N]) { init_from_nocopy(str); } // hack on a hack - char buf[32] isn't a literal
#else
	cstring(const char * str) { init_from_nocopy(str); }
#endif
	
	cstring(arrayview<uint8_t> bytes) { init_from_nocopy(bytes); }
	cstring(arrayview<char> chars) { init_from_nocopy(chars.reinterpret<uint8_t>()); }
	cstring(nullptr_t) { init_empty(); }
	// If has_nul, then bytes[bytes.size()] is zero. (Undefined behavior does not count as zero.)
	cstring(arrayview<uint8_t> bytes, bool has_nul) { init_from_nocopy(bytes, has_nul); }
	cstring& operator=(const cstring& other) = default;
	cstring& operator=(const char * str) { init_from_nocopy(str); return *this; }
	cstring& operator=(nullptr_t) { init_empty(); return *this; }
	
	explicit operator bool() const { return length() != 0; }
	//explicit operator const char * () const { return ptr_withnul(); }
	
	char operator[](int index) const { return getchar(index); }
	
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
		return cstring(arrayview<uint8_t>(ptr()+start, end-start), (bytes_hasterm() && (uint32_t)end == length()));
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
	
	//crsplitwi - cstring-returning backwards-counting split on word boundaries, inclusive
	//cstring-returning - obvious
	//backwards-counting - splits at the rightmost opportunity, "a b c d".rsplit<1>(" ") is ["a b c", "d"]
	//word boundary - isspace()
	//inclusive - the boundary string is included in the output, "a\nb\n".spliti("\n") is ["a\n", "b\n"]
	//all subsets of splitting options are supported
	
	array<cstring> csplit(cstring sep, size_t limit) const;
	template<size_t limit = SIZE_MAX>
	array<cstring> csplit(cstring sep) const { return csplit(sep, limit); }
	
	array<cstring> crsplit(cstring sep, size_t limit) const;
	template<size_t limit>
	array<cstring> crsplit(cstring sep) const { return crsplit(sep, limit); }
	
	array<string> split(cstring sep, size_t limit) const { return csplit(sep, limit).cast<string>(); }
	template<size_t limit = SIZE_MAX>
	array<string> split(cstring sep) const { return split(sep, limit); }
	
	array<string> rsplit(cstring sep, size_t limit) const { return crsplit(sep, limit).cast<string>(); }
	template<size_t limit>
	array<string> rsplit(cstring sep) const { return rsplit(sep, limit); }
	
	
	array<cstring> cspliti(cstring sep, size_t limit) const;
	template<size_t limit = SIZE_MAX>
	array<cstring> cspliti(cstring sep) const { return cspliti(sep, limit); }
	
	array<cstring> crspliti(cstring sep, size_t limit) const;
	template<size_t limit>
	array<cstring> crspliti(cstring sep) const { return crspliti(sep, limit); }
	
	array<string> spliti(cstring sep, size_t limit) const { return cspliti(sep, limit).cast<string>(); }
	template<size_t limit = SIZE_MAX>
	array<string> spliti(cstring sep) const { return spliti(sep, limit); }
	
	array<string> rspliti(cstring sep, size_t limit) const { return crspliti(sep, limit).cast<string>(); }
	template<size_t limit>
	array<string> rspliti(cstring sep) const { return rspliti(sep, limit); }
	
	//TODO: do I need these?
	
	//array<cstring> csplitw(size_t limit) const;
	//template<size_t limit = SIZE_MAX>
	//array<cstring> csplitw() const { return csplitw(limit); }
	//
	//array<cstring> crsplitw(size_t limit) const;
	//template<size_t limit>
	//array<cstring> crsplitw() const { return crsplitw(limit); }
	//
	//array<string> splitw(size_t limit) const { return csplitw(limit).cast<string>(); }
	//template<size_t limit = SIZE_MAX>
	//array<string> splitw() const { return splitw(limit); }
	//
	//array<string> rsplitw(size_t limit) const { return crsplitw(limit).cast<string>(); }
	//template<size_t limit>
	//array<string> rsplitw() const { return rsplitw(limit); }
	
private:
	// Input: Three pointers, start <= at <= end. The found match must be within the incoming at..end.
	// Output: Set at/end.
	array<cstring> csplit(bool(*find)(const uint8_t * start, const uint8_t * & at, const uint8_t * & end), size_t limit) const;
	
public:
	template<typename T>
	std::enable_if_t<sizeof(T::match(nullptr,nullptr,nullptr))!=0, array<cstring>>
	csplit(T regex, size_t limit) const
	{
		return csplit([](const uint8_t * start, const uint8_t * & at, const uint8_t * & end)->bool {
			auto cap = T::match((char*)start, (char*)at, (char*)end);
			if (!cap) return false;
			at = (uint8_t*)cap[0].start;
			end = (uint8_t*)cap[0].end;
			return true;
		}, limit);
	}
	template<size_t limit = SIZE_MAX, typename T>
	std::enable_if_t<sizeof(T::match(nullptr,nullptr,nullptr))!=0, array<cstring>>
	csplit(T regex) const { return csplit(regex, limit); }
	
	template<typename T>
	std::enable_if_t<sizeof(T::match(nullptr,nullptr,nullptr))!=0, array<string>>
	split(T regex, size_t limit) const
	{
		return csplit(regex, limit).template cast<string>();
	}
	template<size_t limit = SIZE_MAX, typename T>
	std::enable_if_t<sizeof(T::match(nullptr,nullptr,nullptr))!=0, array<string>>
	split(T regex) const { return split(regex, limit); }
	
	string upper() const; // Only considers ASCII, will not change ø. Will capitalize a decomposed ñ, but not a precomposed one.
	string lower() const;
	cstring trim() const; // Deletes whitespace at start and end. Does not do anything to consecutive whitespace in the middle.
	bool contains_nul() const;
	
	bool isutf8() const; // NUL is considered valid UTF-8. U+D800, overlong encodings, etc are not.
	// Treats the string as UTF-8 and returns the codepoint there.
	// If not UTF-8 or points to the middle of a character, returns U+DC80 through U+DCFF. Callers are welcome to treat this as an error.
	// The index is updated to point to the next codepoint. Initialize it to zero; stop when it equals the string's length.
	// If index is out of bounds, returns 'eof' and does not advance index. Return value is signed only so eof can be -1.
	// If the string contains 00s, this function will treat it as U+0000. Callers are welcome to explicitly reject that.
	int32_t codepoint_at(uint32_t& index, int32_t eof = -1) const;
	
	//Whether the string matches a glob pattern. ? in 'pat' matches any byte (not utf8 codepoint), * matches zero or more bytes.
	//NUL bytes are treated as any other byte, in both strings.
	bool matches_glob(cstring pat) const __attribute__((pure)) { return matches_glob(pat, false); }
	// Case insensitive. Considers ASCII only, øØ are considered nonequal.
	bool matches_globi(cstring pat) const __attribute__((pure)) { return matches_glob(pat, true); }
private:
	bool matches_glob(cstring pat, bool case_insensitive) const __attribute__((pure));
public:
	
	string leftPad (size_t len, uint8_t ch = ' ') const;
	
	size_t hash() const { return ::hash(ptr(), length()); }
	
private:
	class c_string {
		char* ptr;
		bool do_free;
	public:
		
		c_string(arrayview<uint8_t> data, bool has_term)
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
		~c_string() { if (do_free) free(ptr); }
	};
public:
	//no operator const char *, a cstring doesn't necessarily have a NUL terminator
	c_string c_str() const { return c_string(bytes(), bytes_hasterm()); }
};


class string : public cstring {
	friend class cstring;
	
	static size_t bytes_for(uint32_t len) { return bitround(len+1); }
	forceinline uint8_t * ptr() { return (uint8_t*)cstring::ptr(); }
	forceinline const uint8_t * ptr() const { return cstring::ptr(); }
	void resize(uint32_t newlen);
	forceinline const char * ptr_withnul() const { return (char*)ptr(); }
	
	void init_from(const char * str)
	{
		if (!str) debug_warn_stack("init_from(null)");
		if (!str) str = "";
		init_from((uint8_t*)str, strlen(str));
	}
	void init_from(const uint8_t * str, uint32_t len)
	{
		if (__builtin_constant_p(len))
		{
			if (len <= MAX_INLINE)
			{
				memcpy(m_inline, str, len);
				m_inline[len] = '\0';
				m_inline_len_w() = max_inline()-len;
			}
			else init_from_large(str, len);
		}
		else init_from_outline(str, len);
	}
	void init_from(arrayview<uint8_t> data) { init_from(data.ptr(), data.size()); }
	void init_from_outline(const uint8_t * str, uint32_t len);
	void init_from_large(const uint8_t * str, uint32_t len);
	void init_from(const cstring& other);
	void init_from(string&& other)
	{
		memcpy((void*)this, (void*)&other, sizeof(*this));
		other.init_empty();
	}
	
	void reinit_from(const char * str)
	{
		if (!str) str = "";
		reinit_from(arrayview<uint8_t>((uint8_t*)str, strlen(str)));
	}
	void reinit_from(arrayview<uint8_t> data);
	void reinit_from(cstring other)
	{
		reinit_from(other.bytes());
	}
	void reinit_from(string&& other)
	{
		release();
		memcpy((void*)this, (void*)&other, sizeof(*this));
		other.init_empty();
	}
	
	void release()
	{
		if (!inlined()) free(m_data);
	}
	
	void append(arrayview<uint8_t> newdat)
	{
		// cache these four, for performance
		uint8_t* p1 = ptr();
		const uint8_t* p2 = newdat.ptr();
		uint32_t l1 = length();
		uint32_t l2 = newdat.size();
		
		if (UNLIKELY(p2 >= p1 && p2 < p1+l1))
		{
			uint32_t offset = p2-p1;
			resize(l1+l2);
			p1 = ptr();
			memcpy(p1+l1, p1+offset, l2);
		}
		else
		{
			resize(l1+l2);
			memcpy(ptr()+l1, p2, l2);
		}
	}
	
	void append(uint8_t newch)
	{
		uint32_t oldlength = length();
		resize(oldlength + 1);
		ptr()[oldlength] = newch;
	}
	
public:
	//Resizes the string to a suitable size, then allows the caller to fill it in. Initial contents are undefined.
	arrayvieww<uint8_t> construct(uint32_t len)
	{
		resize(len);
		return bytes();
	}
	
	string& operator+=(const char * right)
	{
		append(arrayview<uint8_t>((uint8_t*)right, strlen(right)));
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
	
	
	string() : cstring(noinit()) { init_empty(); }
	string(const string& other) : cstring(noinit()) { init_from(other); }
	string(string&& other) : cstring(noinit()) { init_from(std::move(other)); }
	
#if __GNUC__ == 7
	// disgusting hack to help gcc optimize string literal arguments properly
	template<size_t N> void init_from_literal(const char (&str)[N])
	{
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 8 && __GNUC__ <= 10 && __cplusplus >= 201103
		// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=91212
		init_from(arrayview<uint8_t>((uint8_t*)str, strlen(str)));
#else
		init_from(arrayview<uint8_t>((uint8_t*)str, N-1));
#endif
	}
	template<typename T, typename Ttest = std::enable_if_t<std::is_same_v<T,const char*> || std::is_same_v<T,char*>>>
	string(T str) : cstring(noinit()) { init_from(str); } // disgusting hack to optimize string literal arguments
	template<size_t N> string(const char (&str)[N]) : cstring(noinit()) { init_from_literal<N>(str); }
	template<size_t N> string(char (&str)[N]) : cstring(noinit()) { init_from(str); } // hack on a hack - char buf[32] isn't a literal
#else
	string(const char * str) : cstring(noinit()) { init_from(str); }
#endif
	
	string(cstring other) : cstring(noinit()) { init_from(other); }
	string(arrayview<uint8_t> bytes) : cstring(noinit()) { init_from(bytes); }
	string(arrayview<char> chars) : cstring(noinit()) { init_from(chars.reinterpret<uint8_t>()); }
	string(array<uint8_t>&& bytes);
	string(nullptr_t) : cstring(noinit()) { init_empty(); }
	string& operator=(const string& other) { reinit_from(other); return *this; }
	string& operator=(const cstring& other) { reinit_from(other); return *this; }
	string& operator=(string&& other) { reinit_from(std::move(other)); return *this; }
	string& operator=(const char * str) { reinit_from(str); return *this; }
	string& operator=(nullptr_t) { release(); init_empty(); return *this; }
	~string() { release(); }
	
	explicit operator bool() const { return length() != 0; }
	operator const char * () const { return ptr_withnul(); }
	
	//Reading the NUL terminator is fine. Writing the terminator, or poking beyond the NUL, is undefined behavior.
	forceinline uint8_t& operator[](int index) { return ptr()[index]; }
	forceinline uint8_t operator[](int index) const { return ptr()[index]; }
	
	forceinline arrayview<uint8_t> bytes() const { return bytes_raw(); }
	forceinline arrayvieww<uint8_t> bytes() { return bytes_raw(); }
	
	//Takes ownership of the given pointer. Will free() it when done.
	static string create_usurp(char * str);
	static string create_usurp(array<uint8_t>&& in) { return string(std::move(in)); }
	
	//Returns a string containing a single NUL.
	static cstring nul() { return arrayview<uint8_t>((uint8_t*)"", 1); }
	
	//Returns U+FFFD for UTF16-reserved inputs. 0 yields a NUL byte.
	static string codepoint(uint32_t cp);
	// Returns number of bytes written. Buffer must be at least 4 bytes. Does not NUL terminate.
	// May write garbage between out+return and out+4.
	static size_t codepoint(uint8_t* out, uint32_t cp);
	
	//3-way comparison. If a comes first, return value is negative; if equal, zero; if b comes first, positive.
	//Comparison is bytewise. End goes before NUL, so the empty string comes before everything else.
	//The return value is not guaranteed to be in [-1..1]. It's not even guaranteed to fit in anything smaller than int.
	static int compare3(cstring a, cstring b);
	//Like the above, but case insensitive. Considers ASCII only, øØ are considered nonequal.
	//If the strings are case-insensitively equal, uppercase goes first.
	static int icompare3(cstring a, cstring b);
	static bool less(cstring a, cstring b) { return compare3(a, b) < 0; }
	static bool iless(cstring a, cstring b) { return icompare3(a, b) < 0; }
	
	//Natural comparison; "8" < "10". Other than that, same as above.
	//Exact rules:
	//  Strings are compared component by component. A component is either a digit sequence, or a non-digit. 8 < 10, 2 = 02
	//  - and . are not part of the digit sequence. -1 < -2, 1.2 < 1.03
	//  If the strings are otherwise equal, repeat the comparison, but with 2 < 02. If still equal, let A < a.
	//Correct sorting is a1 a2 a02 a2a a2a1 a02a2 a2a3 a2b a02b A3A A3a a3A a3a A03A A03a a03A a03a a10 a11 aa
	static int natcompare3(cstring a, cstring b) { return string::natcompare3(a, b, false); }
	static int inatcompare3(cstring a, cstring b) { return string::natcompare3(a, b, true); }
	static bool natless(cstring a, cstring b) { return natcompare3(a, b) < 0; }
	static bool inatless(cstring a, cstring b) { return inatcompare3(a, b) < 0; }
private:
	static int natcompare3(cstring a, cstring b, bool case_insensitive);
public:
};

// TODO: I need a potentially-owning string class
// cstring never owns memory, string always does, new one has a flag for whether it does
// it's like a generalized cstring::c_str()
// will be immutable after creation, like cstring
// will be used for bml parser, punycode, and most likely a lot more
// need to find a good name for it first

#undef OBJ_SIZE
#undef MAX_INLINE


// using cstring rather than const cstring& is cleaner, but makes GCC 7 emit slightly worse code
// TODO: check if that's still true for GCC > 7
#if __GNUC__ == 7
// TODO: delete friend declaration when deleting this
template<size_t N> inline bool operator==(const cstring& left, char (&right)[N]) { return operator==(left, (const char*)right); }
template<size_t N> inline bool operator==(const cstring& left, const char (&right)[N])
{
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 8 && __GNUC__ <= 10 && __cplusplus >= 201103
	// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=91212
	return operator==(left, (const char*)right);
#else
	if (N-1 <= cstring::max_inline())
		return ((uint8_t)left.m_inline_len() == cstring::max_inline()-(N-1) && memeq(left.m_inline, right, N-1));
	else
		return (!left.inlined() && left.m_len == N-1 && memeq(left.m_data, right, N-1));
#endif
}
template<typename T, typename Ttest = std::enable_if_t<std::is_same_v<T,const char*> || std::is_same_v<T,char*>>>
inline bool operator==(const cstring& left, T right) { return left.bytes() == arrayview<uint8_t>((uint8_t*)right, strlen(right)); }
#else
forceinline bool operator==(const cstring& left, const char * right)
{
	size_t len = strlen(right);
	if (__builtin_constant_p(len))
	{
		if (len <= cstring::max_inline())
			return ((uint8_t)left.m_inline_len() == cstring::max_inline()-len && memeq(left.m_inline, right, len));
		else
			return (!left.inlined() && left.m_len == len && memeq(left.m_data, right, len));
	}
	else return left.bytes() == arrayview<uint8_t>((uint8_t*)right, len);
}
#endif

inline bool operator==(const char * left, const cstring& right) { return operator==(right, left); }
inline bool operator==(const cstring& left, const cstring& right)
{
#ifdef __SSE2__
	if (left.inlined())
	{
		static_assert(sizeof(cstring) == 16);
		if (left.m_inline_len() != right.m_inline_len()) return false;
		__m128i a = _mm_loadu_si128((__m128i*)&left);
		__m128i b = _mm_loadu_si128((__m128i*)&right);
		int eq = _mm_movemask_epi8(_mm_cmpeq_epi8(a, b));
		return ((((~eq) << left.m_inline_len()) & 0xFFFF) == 0);
	}
	else return !right.inlined() && arrayview<uint8_t>(left.m_data, left.m_len) == arrayview<uint8_t>(right.m_data, right.m_len);
#else
	return left.bytes() == right.bytes();
#endif
}
inline bool operator!=(const cstring& left,      const char * right) { return !operator==(left, right); }
inline bool operator!=(const cstring& left,      const cstring& right     ) { return !operator==(left, right); }
inline bool operator!=(const char * left, const cstring& right     ) { return !operator==(left, right); }

bool operator<(cstring left,      const char * right) = delete;
bool operator<(cstring left,      cstring right     ) = delete;
bool operator<(const char * left, cstring right     ) = delete;

inline string operator+(cstring left,      cstring right     ) { string ret=left; ret+=right; return ret; }
inline string operator+(cstring left,      const char * right) { string ret=left; ret+=right; return ret; }
inline string operator+(string&& left,     cstring right     ) { left+=right; return left; }
inline string operator+(string&& left,     const char * right) { left+=right; return left; }
inline string operator+(const char * left, cstring right     ) { string ret=left; ret+=right; return ret; }

inline string operator+(string&& left, char right) = delete;
inline string operator+(cstring left, char right) = delete;
inline string operator+(char left, cstring right) = delete;

//Checks if needle is one of the 'separator'-separated words in the haystack. The needle may not contain 'separator' or be empty.
//For example, haystack "GL_EXT_FOO GL_EXT_BAR GL_EXT_QUUX" (with space as separator) contains needles
// 'GL_EXT_FOO', 'GL_EXT_BAR' and 'GL_EXT_QUUX', but not 'GL_EXT_QUU'.
bool strtoken(const char * haystack, const char * needle, char separator);

template<typename T>
cstring arrayview<T>::get_or(size_t n, const char * def) const
{
	if (n < count) return items[n];
	else return def;
};

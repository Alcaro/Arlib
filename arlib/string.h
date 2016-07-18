#ifndef ARLIB_STRING_H
#define ARLIB_STRING_H
#include "global.h"
#include <string.h>
#include <valgrind/memcheck.h>

//A string owns its storage. cstring doesn't, but is a bit faster.
//It is recommended to use cstring for arguments and string for return values.

class cstring;
#if 0
//Reference string implementation - slow but simple
//All of these functions, including private functions but not including the members, must be present in a complaint string class

class string {
private:
	char* ptr;
	size_t len;
	
	//cstring uses the nocopy and null constructors
	friend class cstring;
	
	void init_from(const char * str)
	{
		ptr = strdup(str);
		len = strlen(str);
	}
	void init_from(const char * str, uint32_t len)
	{
		this->len = len;
		ptr = malloc(len+1);
		memcpy(ptr, str, len);
		ptr[len]='\0';
	}
	void init_from(const string& other) { init_from(other.ptr); }
	void init_from_nocopy(const char * str) { init_from(str); }
	void init_from_nocopy(const char * str, uint32_t len) { init_from(str, len); }
	void init_from_nocopy(const string& other) { init_from(other); }
	void release() { free(ptr); }
	
	//constant for all string implementations, but used by the implementation, so let's keep it here
	int32_t realpos(int32_t pos) const
	{
		if (pos >= 0) return pos;
		else return length()-~pos;
	}
	
	char getchar(int32_t index) const { return ptr[realpos(index)]; }
	void setchar(int32_t index_, char val)
	{
		uint32_t index = realpos(index_);
		if (index==len)
		{
			len++;
			ptr = realloc(ptr, len+1);
			ptr[len] = 0;
		}
		ptr[index] = val;
		if (val == '\0') len = index;
	}
	
	//wstring uses these two plus the public API
	friend class wstring;
	bool wcache() const { return false; }
	void wcache(bool newval) const {}
	
public:
	//NUL terminated
	const char * data() const { return ptr; }
	uint32_t length() const { return strlen(ptr); }
	
	//Non-terminated
	const char * nt() const { return ptr; }
	
	void replace(int32_t pos, int32_t len, cstring newdat)
	{
		pos = realpos(pos);
		len = realpos(len);
		
		const char * part1 = ptr;
		size_t len1 = pos;
		
		const char * part2 = newdat.ptr;
		size_t len2 = newdat.len;
		
		const char * part3 = ptr+pos+len;
		size_t len3 = strlen(part3);
		
		char* newptr = malloc(len1+len2+len3+1);
		memcpy(newptr, part1, len1);
		memcpy(newptr+len1, part2, len2);
		memcpy(newptr+len1+len2, part3, len3);
		newptr[len1+len2+len3]='\0';
		
		free(ptr);
		ptr = newptr;
		len = len1+len2+len3;
	}
	
	string& operator+=(const char * right)
	{
		char* ret = malloc(len+strlen(right)+1);
		memcpy(ret, ptr, len);
		strcpy(ret+len, right);
		len += strlen(right);
		free(ptr);
		ptr = ret;
		return *this;
	}
	
	string& operator+=(const string& right)
	{
		this->operator+=(right.data());
		return *this;
	}
	
#define ARLIB_STRING_FUNCS
#include "string.h"
};

#define ARLIB_STRING_GLOBALS
#include "string.h"

#else

//Optimized implementation - fast but unreadable
class string {
	static const int obj_size = 16; // maximum 120, or the inline length overflows
	                                // (127 would fit, but that requires an extra alignment byte, which throws the sizeof assert)
	static const int max_inline = obj_size-1;
	
	union {
		struct {
			char m_inline[max_inline];
			
			//this is how many bytes are unused by the raw string data
			//if all bytes are used, there are zero unused bytes - which also serves as the NUL
			//if not inlined, last byte is -1
			char m_inline_len;
		};
		struct {
			char* m_data; // if owning, there's also a int32 refcount before this pointer; if not owning, no such thing
			uint32_t m_len;
			bool m_owning;
			bool m_nul; // whether the string is properly terminated (always true if owning)
			mutable bool m_wcache; // could use bitfields here, but no point, there's nothing else I need those extra bytes for
			char reserved; // matches the last byte of the inline data; never ever access this
		};
	};
	
	bool inlined() const
	{
		static_assert(sizeof(string)==obj_size);
		
		return m_inline_len != (char)-1;
	}
	
	const char * ptr() const
	{
		if (inlined()) return m_inline;
		else return m_data;
	}
	
	char* ptr()
	{
		if (inlined()) return m_inline;
		else return m_data;
	}
	
	static size_t bytes_for(uint32_t len)
	{
		return bitround(sizeof(int)+len+1);
	}
	
	
	void unshare() const
	{
		wcache(false);
		//TODO
		//if (inlined()) return;
		//if (m_owning && *refcount()==1) return;
		//release();
	}
	
	//does not initialize the new data
	void resize(uint32_t newlen)
	{
		unshare();
		
		switch (!inlined()<<1 | (newlen>max_inline))
		{
		case 0: // small->small
			{
				m_inline[newlen] = '\0';
				m_inline_len = max_inline-newlen;
			}
			break;
		case 1: // small->big
			{
				char* newptr = malloc(newlen+1);
				memcpy(newptr, m_inline, max_inline);
				newptr[newlen] = '\0';
				m_data = newptr;
				m_len = newlen;
				m_owning = true;
				m_nul = true;
				m_wcache = false;
				
				m_inline_len = -1;
			}
			break;
		case 2: // big->small
			{
				char* oldptr = m_data;
				memcpy(m_inline, oldptr, newlen);
				free(oldptr);
				m_inline[newlen] = '\0';
				m_inline_len = max_inline-newlen;
			}
			break;
		case 3: // big->big
			{
				m_data = realloc(m_data, newlen+1);
				m_data[newlen] = '\0';
				m_len = newlen;
			}
			break;
		}
	}
	
public:
	//NUL terminated
	const char * data() const
	{
		if (!inlined() && !m_nul)
		{
			unshare();
		}
		return ptr();
	}
	uint32_t length() const
	{
		if (inlined()) return max_inline-m_inline_len;
		else return m_len;
	}
	
	//Non-terminated
	const char * nt() const
	{
		return ptr();
	}
	
private:
	//cstring uses the nocopy constructors
	friend class cstring;
	
	void init_from(const char * str)
	{
		//TODO
		init_from(str, strlen(str));
	}
	void init_from(const char * str, uint32_t len)
	{
		if (len <= max_inline)
		{
			memcpy(m_inline, str, len);
			m_inline[len] = '\0';
			m_inline_len = max_inline-len;
		}
		else
		{
			m_inline_len = -1;
			
			m_data = malloc(len+1);
			memcpy(m_data, str, len);
			m_data[len]='\0';
			
			m_len = len;
			m_owning = true;
			m_nul = true;
			m_wcache = false;
		}
	}
	void init_from(const string& other)
	{
		//TODO
		init_from(other.data());
	}
	void init_from_nocopy(const char * str)
	{
		init_from_nocopy(str, strlen(str));
		if (!inlined()) m_nul = true;
	}
	void init_from_nocopy(const char * str, uint32_t len)
	{
		if (len <= max_inline)
		{
			memcpy(m_inline, str, len);
			m_inline[len] = '\0';
			m_inline_len = max_inline-len;
		}
		else
		{
			m_inline_len = -1;
			
			m_data = (char*)str;
			m_len = len;
			m_owning = false;
			m_nul = false;
			m_wcache = false;
		}
	}
	void init_from_nocopy(const string& other)
	{
		memcpy(this, &other, sizeof(*this));
		if (!inlined()) m_owning=false;
	}
	void release()
	{
		if (!inlined() && m_owning)
		{
			free(m_data);
		}
	}
	
	//constant for all string implementations, but used by the implementation, so let's keep it here
	int32_t realpos(int32_t pos) const
	{
		if (pos >= 0) return pos;
		else return length()-~pos;
	}
	
	char getchar(int32_t index_) const
	{
		uint32_t index = realpos(index_);
		if (index == length()) return '\0';
		else return ptr()[index];
	}
	void setchar(int32_t index_, char val)
	{
		unshare();
		uint32_t index = realpos(index_);
		if (index == length())
		{
			resize(index+1);
		}
		ptr()[index] = val;
	}
	
	//wstring uses these two plus the public API
	friend class wstring;
	bool wcache() const
	{
		if (inlined()) return false;
		else return m_wcache;
	}
	void wcache(bool newval) const
	{
		if (!inlined()) m_wcache = newval;
	}
	
	void append(const char * newdat, uint32_t newlength)
	{
		if (newdat >= ptr() && newdat < ptr()+length())
		{
			uint32_t offset = newdat-ptr();
			uint32_t oldlength = length();
			resize(oldlength+newlength);
			memcpy(ptr()+oldlength, ptr()+offset, newlength);
		}
		else
		{
			uint32_t oldlength = length();
			resize(oldlength+newlength);
			memcpy(ptr()+oldlength, newdat, newlength);
		}
	}
	
public:
	void replace(int32_t pos, int32_t len, const string& newdat)
	{
		//TODO: what if newdat is (a subset of) 'this'?
		//if 'this' is a proper subset of newdat, then !m_owning, so freeing it doesn't affect newdat; we can ignore that
		
		uint32_t prevlength = length();
		uint32_t newlength = newdat.length();
		
		if (newlength < prevlength)
		{
			unshare();
			memmove(ptr()+pos+newlength, ptr()+pos+len, prevlength-len-pos);
			resize(prevlength - len + newlength);
		}
		if (newlength == prevlength)
		{
			unshare();
		}
		if (newlength > prevlength)
		{
			resize(prevlength - len + newlength);
			memmove(ptr()+pos+newlength, ptr()+pos+len, prevlength-len-pos);
		}
		
		memcpy(ptr()+pos, newdat.ptr(), newlength);
	}
	
	string& operator+=(const char * right)
	{
		append(right, strlen(right));
		return *this;
	}
	
	string& operator+=(const string& right)
	{
		append(right.ptr(), right.length());
		return *this;
	}
#define ARLIB_STRING_FUNCS
#include "string.h"
};

#define ARLIB_STRING_GLOBALS
#include "string.h"
#endif
#endif

#ifdef ARLIB_STRING_FUNCS
#undef ARLIB_STRING_FUNCS
//Shared between all string implementations.
private:
	class noinit {};
	string(noinit) {}
	
public:
	string() { init_from(""); }
	string(const string& other) { init_from(other); }
	string(const char * str) { init_from(str); }
	string(const char * str, uint32_t len) { init_from(str, len); }
	string& operator=(const string& other) { release(); init_from(other); return *this; }
	string& operator=(const char * str) { release(); init_from(str); return *this; }
	~string() { release(); }
	
	operator bool() const { return length(); }
	operator const char * () const { return data(); }
	
private:
	class charref {
		string* parent;
		uint32_t index;
		
	public:
		charref& operator=(char ch) { parent->setchar(index, ch); return *this; }
		operator char() { return parent->getchar(index); }
		
		charref(string* parent, uint32_t index) : parent(parent), index(index) {}
	};
	friend class charref;
	
public:
	//Reading the NUL terminator is fine. Writing extends the string. Poking outside the string is undefined.
	charref operator[](uint32_t index) { return charref(this, index); }
	charref operator[](int index) { return charref(this, index); }
	char operator[](uint32_t index) const { return getchar(index); }
	char operator[](int index) const { return getchar(index); }
	
	static string create(const char * data, uint32_t len) { string ret=noinit(); ret.init_from(data, len); return ret; }
	
	string substr(int32_t start, int32_t end) const
	{
		start = realpos(start);
		end = realpos(end);
		return string(data()+start, end-start);
	}
	inline cstring csubstr(int32_t start, int32_t end) const;
#endif

#ifdef ARLIB_STRING_GLOBALS
#undef ARLIB_STRING_GLOBALS
inline bool operator==(const string& left, const char * right ) { return !strcmp(left.data(), right); }
inline bool operator==(const string& left, const string& right) { return !strcmp(left.data(), right.data()); }
inline bool operator==(const char * left,  const string& right) { return !strcmp(left, right.data()); }
inline bool operator!=(const string& left, const char * right ) { return  strcmp(left.data(), right); }
inline bool operator!=(const string& left, const string& right) { return  strcmp(left.data(), right.data()); }
inline bool operator!=(const char * left,  const string& right) { return  strcmp(left, right.data()); }

inline string operator+(string&& left, const char * right) { left+=right; return left; }
inline string operator+(const string& left, const char * right) { string ret=left; ret+=right; return ret; }
inline string operator+(string&& left, const string& right) { left+=right; return left; }
inline string operator+(const string& left, const string& right) { string ret=left; ret+=right; return ret; }
inline string operator+(const char * left, const string& right) { string ret=left; ret+=right; return ret; }

class cstring : public string {
public:
	cstring() : string() {}
	cstring(const string& other) : string(noinit()) { init_from_nocopy(other); }
	cstring(const char * str) : string(noinit()) { init_from_nocopy(str); }
};

inline cstring string::csubstr(int32_t start, int32_t end) const
{
	start = realpos(start);
	end = realpos(end);
	return string(data()+start, end-start);
}

//TODO
class wstring : public string {
	mutable uint32_t pos_bytes;
	mutable uint32_t pos_chars;
	mutable uint32_t wsize;
	//char pad[4];
	const uint32_t WSIZE_UNKNOWN = -1;
	
	void clearcache() const
	{
		pos_bytes = 0;
		pos_chars = 0;
		wsize = WSIZE_UNKNOWN;
		wcache(true);
	}
	
	void checkcache() const
	{
		if (!wcache()) clearcache();
	}
	
	uint32_t findcp(uint32_t index) const
	{
		checkcache();
		
		if (pos_chars > index)
		{
			pos_bytes=0;
			pos_chars=0;
		}
		
		uint8_t* scan = (uint8_t*)data() + pos_bytes;
		uint32_t chars = pos_chars;
		while (chars != index)
		{
			if ((*scan&0xC0) != 0x80) chars++;
			scan++;
		}
		pos_bytes = scan - (uint8_t*)data();
		pos_chars = index;
		
		return pos_bytes;
	}
	
	uint32_t getcp(uint32_t index) const { return 42; }
	void setcp(uint32_t index, uint32_t val) { }
	
	class charref {
		wstring* parent;
		uint32_t index;
		
	public:
		charref& operator=(char ch) { parent->setcp(index, ch); return *this; }
		operator uint32_t() { return parent->getcp(index); }
		
		charref(wstring* parent, uint32_t index) : parent(parent), index(index) {}
	};
	friend class charref;
	
public:
	wstring() : string() { clearcache(); }
	wstring(const string& other) : string(other) { clearcache(); }
	wstring(const char * str) : string(str) { clearcache(); }
	
	charref operator[](uint32_t index) { return charref(this, index); }
	charref operator[](int index) { return charref(this, index); }
	uint32_t operator[](uint32_t index) const { return getcp(index); }
	uint32_t operator[](int index) const { return getcp(index); }
	
	uint32_t size() const
	{
		checkcache();
		if (wsize == WSIZE_UNKNOWN)
		{
			uint8_t* scan = (uint8_t*)data() + pos_bytes;
			uint32_t chars = pos_chars;
			while (*scan)
			{
				if ((*scan&0xC0) != 0x80) chars++;
				scan++;
			}
			wsize = chars;
		}
		return wsize;
	}
};
#endif

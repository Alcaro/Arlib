#pragma once
#include "global.h"
#include "array.h"
#include "set.h"

// Arlib regexes are same as JS version 5.1 regexes (which are mostly same as C++ regexes), with the following differences:
// - No Unicode, and especially no UTF-16, only bytes. \s matches only ASCII space and some control characters; \u1234 is not implemented.
//     Byte values 128-255 are considered non-space, non-alphanumeric; \D \S \W will match them.
//     You can put literal UTF-8 strings in your regex, or for complex cases, match UTF-8 byte values using \x.
// - No extensions beyond v5.1. The following are absent:
//   - Lookbehind (?<=expr) (?<!expr) (difficult to implement, and rare)
//   - Named capture groups (?<name>expr), \k<name> (hard to represent that return value in C++)
//   - Unicode property groups \p{Sc} (would require large lookup tables, and incompatible with my byte-oriented approach)
//   - Character class expressions [abc--b], [abc&&ace] (useless without \p)
//   - Nesting anything in character sets [\q{abc}] (difficult to implement, near useless without \p)
//   - Probably some more stuff, the spec is increasingly difficult to read
// - Illegal backreferences (backreferences that cannot be defined at this point) are errors, not synonyms for empty string.
// - No exceptions, it just returns false.
// - None of the C++ extensions. No regex traits, no named character classes, collation, or equivalence. [[:digit:]], [[.tilde.]], [[=a=]]
// - None of the browser compatibility extensions; for example, {a} is an error, not literal chars.
// - There may be bugs, of course. I can think of plenty of troublesome edge cases, and it's often difficult to determine the right answer.

// This is fundamentally a backtracking regex engine; as such, the usual caveats about catastrophic backtracking apply.
// However, it optimizes what it can to NFAs, which reduces the asymptote for some regexes.
// For example, (?:a+a+a+a+) will be processed in O(n), not O(n^4), and (?:)+ will be deleted;
//    however, if there are capture groups or other things an NFA can't handle, it will fall back to backtracking.
//    For example, ()+ will crash with a stack overflow.

// If you'd rather not compile the regex every time the expression is reached, you can call this instead.
// This moves the initialization to before main().
#define REGEX(str) (precompiled_regex<decltype([]{ return "" str ""; })>)
#define REGEX_SEARCH(str) (precompiled_regex_search<decltype([]{ return "" str ""; })>)

class regex {
	friend class regex_search;
	
	enum insntype_t {
		t_jump, // Target is 'data'.
		t_accept, // Doesn't use 'data'. Sets the end of capture group 0.
		t_alternative_first, // Try matching starting from 'data'. If it fails, try from the next insn instead.
		t_alternative_second, // Try matching starting from the next insn. If it fails, try from 'data' instead.
		
		t_byte, // 'data' is index to bytes or dfas, respectively.
		t_dfa_shortest, // Shortest match first.
		t_dfa_longest,
		
		t_capture_start, // 'data' is the capture index. 0 is illegal.
		t_capture_end, // If backtracked, undoes the update of the capture.
		t_capture_discard, // Also undoes if backtracked. Used before | (so (?:(a)|b){2} + "ab" doesn't capture), and after (?!(a)) (it matched, no backtracking).
		t_capture_backref, // Doesn't write the capture list, but still capture related, so it goes in this group.
		
		t_assert_boundary, // These don't use 'data'.
		t_assert_notboundary,
		t_assert_start,
		t_assert_end,
		
		t_lookahead_positive, // Lookahead body is after this instruction, ending with t_accept. 'data' tells where to jump if successful.
		t_lookahead_negative,
	};
	struct insn_t {
		insntype_t type;
		uint32_t data;
	};
	
	struct dfa_t {
		// initial state is 0
		// match is 0x80000000 bit, no more matches is 0x7FFFFFFF
		struct node_t {
			uint32_t next[256];
			bool operator==(const node_t& other) const { return arrayview<uint32_t>(next) == arrayview<uint32_t>(other.next); }
			size_t hash() const { return ::hash(arrayview<uint32_t>(next).transmute<uint8_t>()); }
		};
		array<node_t> transitions;
		uint32_t init_state; // either 0 or 0x80000000, depending on whether empty string matches
		
		void dump() const;
	};
	
	uint32_t num_captures;
	array<insn_t> insns;
	array<bitset<256>> bytes; // Simply which bytes are legal at this position. Lots of things compile to this. (Most then become a DFA.)
	array<dfa_t> dfas;
	
public:
	class parser; // not really public, since its implementation is hidden, but some pieces (like regex_search) need access to it
	
public:
	regex() { set_fail(); }
	regex(cstring rgx) { parse(rgx); }
	bool parse(cstring rgx);
	operator bool() const
	{
		return num_captures > 0; // a valid regex has at least the \0 capture, but the set_fail one doesn't
	}
	
private:
	
	void reset()
	{
		insns.reset();
		bytes.reset();
		dfas.reset();
	}
	
	void set_fail() // sets current regex to something that can't match anything
	{
		reset();
		num_captures = 0;
		insns.append({ t_byte, 0 });
		bytes.append();
	}
	
	void optimize();
	
	static bool is_alt(insntype_t type)
	{
		return type == t_alternative_first || type == t_alternative_second;
	}
	static bool targets_another_insn(insntype_t type)
	{
		return is_alt(type) || type == t_jump || type == t_lookahead_positive || type == t_lookahead_negative;
	}
	
	struct pair {
		const char * start;
		const char * end;
		cstring str() const { return arrayview<char>(start, end-start); }
		operator cstring() const { return str(); }
	};
public:
	template<size_t N> class match_t {
		static_assert(N >= 1);
		friend class regex;
		pair group[N];
		size_t m_size;
		
	public:
		template<size_t Ni> friend class match_t;
		match_t() { memset(group, 0, sizeof(group)); }
		template<size_t Ni> match_t(const match_t<Ni>& inner)
		{
			static_assert(Ni < N); // if equal, it goes to the implicitly defaulted copy ctor
			memcpy(group, inner.group, sizeof(inner.group));
			memset(group+Ni, 0, sizeof(pair)*(N-Ni));
		}
		
		size_t size() const { return m_size; }
		const pair& operator[](size_t n) const { return group[n]; }
		operator bool() const { return group[0].end; }
		bool operator!() const { return !(bool)*this; }
	};
	
private:
	class matcher;
	
	enum checktype_t {
		ct_jump,
		ct_dfa_shortest,
		ct_dfa_longest,
		ct_setcapture_start,
		ct_setcapture_end,
		ct_lookahead_pos,
		ct_lookahead_neg,
	};
	struct checkpoint_t {
		checktype_t ty;
		uint32_t state;
		const uint8_t * at;
		size_t extra;
	};
	class my_stack {
		array<checkpoint_t> data;
		size_t size = 0;
	public:
		operator bool() { return size; }
		void reset() { size = 0; }
		forceinline checkpoint_t& pop() { return data[--size]; }
		forceinline void push(checkpoint_t elem)
		{
			if (data.size() == size)
				data.resize((data.size() | 16) * 2);
			data[size++] = elem;
		}
	};
	mutable my_stack checkpoints;
	mutable bitarray dfa_matches;
	
	void match(pair* ret, const char * start, const char * at, const char * end) const;
	void match_alloc(size_t num_captures, pair* ret, const char * start, const char * at, const char * end) const;
	
public:
	// While these match functions are const, they make use of internal caches; this object is not thread safe.
	
	template<size_t n = 5> match_t<n> match(const char * start, const char * at, const char * end) const
	{
		match_t<n> ret {};
		if (LIKELY(n >= this->num_captures))
		{
			ret.m_size = this->num_captures;
			match(ret.group, start, at, end);
		}
		else
		{
			ret.m_size = n;
			match_alloc(n, ret.group, start, at, end);
		}
		return ret;
	}
	template<size_t n = 5> match_t<n> match(const char * start, const char * end) const { return match<n>(start, start, end); }
	template<size_t n = 5> match_t<n> match(const char * str) const { return match<n>(str, str+strlen(str)); }
	// funny type, to ensure it can't construct a temporary
	template<size_t n = 5> match_t<n> match(cstring& str) const { return match<n>(str.ptr_raw(), str.ptr_raw_end()); }
	
	// search() is not recommended, it's slow.
	template<size_t n = 5> match_t<n> search(const char * start, const char * at, const char * end) const
	{
		while (at < end)
		{
			match_t<5> ret = match<n>(start, at, end);
			if (ret)
				return ret;
			at++;
		}
		return {};
	}
	template<size_t n = 5> match_t<n> search(const char * start, const char * end) const { return search<n>(start, start, end); }
	template<size_t n = 5> match_t<n> search(const char * str) const { return search<n>(str, str+strlen(str)); }
	template<size_t n = 5> match_t<n> search(cstring& str) const { return search<n>(str.ptr_raw(), str.ptr_raw_end()); }
	
	string replace(cstring str, const char * replacement) const;
	
public:
	// Prints the compiled regex to stdout. Only usable for debugging.
#ifndef _WIN32
	void dump() const { dump(insns); }
#endif
private:
	void dump(arrayview<insn_t> insns) const;
	static void dump_byte(const bitset<256>& byte);
	static void dump_range(uint8_t first, uint8_t last);
	static void dump_single_char(uint8_t ch);
	
private:
	template<typename T>
	static void required_substrs_recurse(regex* rgx, T& node, array<string>& ret);
public:
	// A string that matches the regex will always contain the returned substrings.
	// Return value will not contain empty strings, but it can be empty.
	// Callers can use this to speed up some operations.
	static array<string> required_substrs(cstring rgx);
};

// A specialized regex class, used to tell the first location where a given regex would match, faster than the regular one.
// However, it can't tell the length of the match, and can't handle captures, backreferences, boundary assertions, or lookarounds.
// (It can tell the length of the match - but only of the shortest match, not the correct one in regex order.)
class regex_search {
	struct next_t {
		uint8_t lowest_possible; // Lowest offset for which any subsequent input sequence can match, or 255 for none.
		uint8_t lowest_match; // Which offset matches here, or 255 for none. If multiple matches, it's the lowest.
		uint16_t next; // If lowest_possible is 255, this is garbage.
		
		bool operator==(const next_t&) const = default;
	};
	struct node_t {
		next_t next[256];
	};
	array<node_t> transitions;
	uint8_t unroll_count;
	bool accept_empty;
	
	void set_fail()
	{
		unroll_count = 0;
		accept_empty = false;
		transitions.resize(1);
		for (int i=0;i<256;i++)
		{
			transitions[0].next[0].lowest_possible = 255;
			transitions[0].next[0].lowest_match = 255;
		}
	}
	
public:
	regex_search() { set_fail(); }
	regex_search(cstring rgx) { parse(rgx); }
private:
	uint16_t state_for_recurse(const bitset<256>& unique_bytes, const regex::dfa_t& dfa,
	                           map<array<int32_t>, uint16_t, arrayview_hasher>& seen, arrayview<int32_t> in_states);
public:
	bool parse(cstring rgx);
	operator bool() const { return unroll_count != 0; }
	
	const char * search(const char * start, const char * at, const char * end) const { return search(at, end); }
	const char * search(const char * start, const char * end) const;
	const char * search(const char * str) const { return search(str, str+strlen(str)); }
	// funny type, to ensure it can't construct a temporary
	const char * search(cstring& str) const { return search(str.ptr_raw(), str.ptr_raw_end()); }
	
	// Prints the compiled regex to stdout. Only usable for debugging.
	void dump() const;
};

template<typename T> static const regex precompiled_regex { T()() };
template<typename T> static const regex_search precompiled_regex_search { T()() };

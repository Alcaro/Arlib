#pragma once
#include "global.h"
#include "array.h"
#include "string.h"
#include "stringconv.h"
#include "set.h"

//This is a streaming parser. It returns a sequence of event objects.
//For example, the document
/*
{ "foo": [ 1, 2, 3 ] }
*/
//would yield { enter_map } { map_key, "foo" } { enter_list } { num, 1 } { num, 2 } { num, 3 } { exit_list } { exit_map }
//The parser keeps trying after an { error }, giving you a partial view of the damaged document; however,
// there are no guarantees on how much you can see, and it is likely for one error to cause many more, or misplaced, nodes.
//enter/exit types are always paired, even in the presense of errors. However, they may be anywhere;
// don't place any expectations on event order inside a map.
//After the document ends, { finish } will be returned forever until the object is deleted.
class jsonparser : nocopy {
public:
	enum {
		unset      = 0,
		jtrue      = 1,
		jfalse     = 2,
		jnull      = 3,
		str        = 4,
		num        = 5,
		enter_list = 6,
		exit_list  = 7,
		enter_map  = 8,
		map_key    = 9,
		exit_map   = 10,
		error      = 11,
		finish     = 12,
	};
	struct event {
		int action;
		string str; // or error message
		double num;
		
		event() : action(unset) {}
		event(int action) : action(action) {}
		event(int action, cstring str) : action(action), str(str) {}
		event(int action, double num) : action(action), num(num) {}
	};
	
	//Remember the cstring rules: If this cstring doesn't hold a reference, don't touch its buffer until the object is disposed.
	//If it's originally a string, don't worry about it.
	//It is not allowed to try to stream data into this object.
	jsonparser(cstring json) : m_data(json) {}
	event next();
	bool errored() { return m_errored; }
	
private:
	cstring m_data;
	size_t m_pos = 0;
	bool m_want_key = false;
	
	bool m_first = true;
	bool m_unexpected_end = false;
	bool m_errored = false;
	event do_error() { m_errored=true; return { error }; }
	
	array<bool> m_nesting; // an entry is false for list, true for map; after [[{, this is false,false,true
	
	uint8_t nextch();
	bool skipcomma(size_t depth = 1);
	
	string getstr();
};


//This is also streaming.
//Calling exit() without a matching enter(), or finish() without closing every enter(), is undefined behavior.
class jsonwriter {
	string m_data;
	bool m_comma = false;
	
	void comma() { if (m_comma) m_data += ','; m_comma = true; }
	
public:
	static string strwrap(cstring s)
	{
		string out = "\"";
		for (size_t i=0;i<s.length();i++)
		{
			char c = s[i];
			if(0);
			else if (c=='\n') out+="\\n";
			else if (c=='"') out += "\\\"";
			else if (c=='\\') out += "\\\\";
			else out += c;
		}
		return out+"\"";
	}
	
	//If you pass in data that's not valid for that mode (for example, val="foo bar" and mode=eq),
	// then it silently switches to the lowest working mode (in the above example, quote).
	//Since enter() implies the tag has children, it will disobey the inline modes; use node() if you want it inlined.
	void null() { comma(); m_data += "null"; }
	void boolean(bool b) { comma(); m_data += b ? "true" : "false"; }
	void str(cstring s) { comma(); m_data += strwrap(s); }
	void num(int n)    { comma(); m_data += tostring(n); }
	void num(size_t n) { comma(); m_data += tostring(n); }
	void num(double n) { comma(); m_data += tostring(n); }
	void list_enter() { comma(); m_data += "["; m_comma = false; }
	void list_exit() { m_data += "]"; m_comma = true; }
	void map_enter() { comma(); m_data += "{"; m_comma = false; }
	void map_key(cstring s) { comma(); m_data += strwrap(s); m_data += ":"; m_comma = false; }
	void map_exit() { m_data += "}"; m_comma = true; }
	
	string finish() { return m_data; }
};



class JSON {
	jsonparser::event ev;
	array<JSON> chld_list;
	map<string,JSON> chld_map;
	
	void construct(jsonparser& p, bool pull = true)
	{
		if (pull) ev = p.next();
//if(pull)puts("1,"+tostring(ev.action));
		if (ev.action == jsonparser::enter_list)
		{
			while (true)
			{
				jsonparser::event next = p.next();
//puts("2,"+tostring(next.action));
				if (next.action == jsonparser::exit_list) break;
				JSON& child = chld_list.append();
				child.ev = next;
				child.construct(p, false);
//if(next.action==jsonparser::finish)return;
			}
		}
		if (ev.action == jsonparser::enter_map)
		{
			while (true)
			{
				jsonparser::event next = p.next();
//puts("3,"+tostring(next.action));
				if (next.action == jsonparser::exit_map) break;
				if (next.action == jsonparser::map_key) chld_map.insert(next.str).construct(p);
//if(next.action==jsonparser::finish)return;
			}
		}
	}
	
	void serialize(jsonwriter& w)
	{
		switch (ev.action)
		{
		case jsonparser::unset:
		case jsonparser::error:
		case jsonparser::jnull:
			w.null();
			break;
		case jsonparser::jtrue:
			w.boolean(true);
			break;
		case jsonparser::jfalse:
			w.boolean(false);
			break;
		case jsonparser::str:
			w.str(ev.str);
			break;
		case jsonparser::num:
			if (ev.num == (int)ev.num) w.num((int)ev.num);
			else w.num(ev.num);
			break;
		case jsonparser::enter_list:
			w.list_enter();
			for (JSON& j : chld_list) j.serialize(w);
			w.list_exit();
			break;
		case jsonparser::enter_map:
			w.map_enter();
			for (auto& e : chld_map)
			{
				w.map_key(e.key);
				e.value.serialize(w);
			}
			w.map_exit();
			break;
		default: abort(); // unreachable
		}
	}
	
public:
	JSON() : ev(jsonparser::jnull) {}
	JSON(cstring s) { parse(s); }
	JSON(const JSON& other) = delete;
	
	void parse(cstring s)
	{
		chld_list.reset();
		chld_map.reset();
		
		jsonparser p(s);
		construct(p);
	}
	
	string serialize()
	{
		jsonwriter w;
		serialize(w);
		return w.finish();
	}
	
	double num() { ev.action = jsonparser::num; return ev.num; }
	string& str() { ev.action = jsonparser::str; return ev.str; }
	array<JSON>& list() { ev.action = jsonparser::enter_list; return chld_list; }
	map<string,JSON>& assoc() { ev.action = jsonparser::enter_map; return chld_map; } // 'map' is taken
	
	operator bool()
	{
		switch (ev.action)
		{
		case jsonparser::unset: return false;
		case jsonparser::jtrue: return true;
		case jsonparser::jfalse: return false;
		case jsonparser::jnull: return false;
		case jsonparser::str: return ev.str;
		case jsonparser::num: return ev.num;
		case jsonparser::enter_list: return chld_list.size();
		case jsonparser::enter_map: return chld_map.size();
		case jsonparser::error: return false;
		default: abort(); // unreachable
		}
	}
	operator int() { return num(); }
	operator size_t() { return num(); }
	operator double() { return num(); }
	operator string() { return str(); }
	//operator cstring() { return str(); }
	//operator const char *() { return str(); }
	
	bool operator==(int right) { return num()==right; }
	bool operator==(size_t right) { return num()==right; }
	bool operator==(double right) { return num()==right; }
	bool operator==(const char * right) { return str()==right; }
	bool operator==(cstring right) { return str()==right; }
	
	JSON& operator=(nullptr_t) { ev.action = jsonparser::jnull; return *this; }
	JSON& operator=(bool b) { ev.action = b ? jsonparser::jtrue : jsonparser::jfalse; return *this; }
	JSON& operator=(size_t n) { return operator=((double)n); }
	JSON& operator=(int n) { return operator=((double)n); }
	JSON& operator=(double n) { ev.action = jsonparser::num; ev.num = n; return *this; }
	JSON& operator=(cstring s) { ev.action = jsonparser::str; ev.str = s; return *this; }
	JSON& operator=(string s) { ev.action = jsonparser::str; ev.str = s; return *this; }
	JSON& operator=(const char * s) { ev.action = jsonparser::str; ev.str = s; return *this; }
	
	JSON& operator[](int n) { return list()[n]; }
	JSON& operator[](size_t n) { return list()[n]; }
	JSON& operator[](const char * s) { return assoc().get_create(s); }
	JSON& operator[](cstring s) { return assoc().get_create(s); }
	
	JSON& operator[](const JSON& right)
	{
		if (right.ev.action == jsonparser::str) return assoc().get_create(right.ev.str);
		else return list()[right.ev.num];
	}
};

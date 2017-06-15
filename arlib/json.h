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
	enum { jtrue, jfalse, jnull, str, num, enter_list, exit_list, enter_map, map_key, exit_map, error, finish };
	struct event {
		int action;
		string str; // or error message
		double num;
		
		event() {}
		event(int action) : action(action) {}
		event(int action, cstring str) : action(action), str(str) {}
		event(int action, double num) : action(action), num(num) {}
	};
	
	//Remember the cstring rules: If this cstring doesn't hold a reference, don't touch its buffer until the object is disposed.
	//If it's originally a string, don't worry about it.
	//It is not allowed to try to stream data into this object.
	jsonparser(cstring json) : m_data(json) {}
	event next();
	
private:
	cstring m_data;
	size_t m_pos = 0;
	bool m_want_key = false;
	bool m_unexpected_end = false;
	array<bool> m_nesting; // an entry is false for list, true for map; after [[{, this is false,false,true
	
	char nextch();
	bool skipcomma();
	
	string getstr();
};



class json {
	jsonparser::event ev;
	array<json> chld_list;
	map<string,json> chld_map;
	
	void construct(jsonparser& p, bool pull = true)
	{
		if (pull) ev = p.next();
		if (ev.action == jsonparser::enter_list)
		{
			while (true)
			{
				jsonparser::event next = p.next();
				if (next.action == jsonparser::exit_list) break;
				json& child = chld_list.append();
				child.ev = next;
				child.construct(p, false);
			}
		}
		if (ev.action == jsonparser::enter_map)
		{
			while (true)
			{
				jsonparser::event next = p.next();
				if (next.action == jsonparser::exit_map) break;
				if (next.action == jsonparser::map_key) chld_map.insert(next.str).construct(p);
			}
		}
	}
	
public:
	json() : ev(jsonparser::jnull) {}
	json(cstring s) { parse(s); }
	
	void parse(cstring s)
	{
		chld_list.reset();
		chld_map.reset();
		
		jsonparser p(s);
		construct(p);
	}
	
	operator bool()
	{
		switch (ev.action)
		{
		case jsonparser::jtrue: return true;
		case jsonparser::jfalse: return false;
		case jsonparser::jnull: return false;
		case jsonparser::str: return ev.str;
		case jsonparser::num: return ev.num;
		case jsonparser::enter_list: return chld_list.size();
		case jsonparser::enter_map: return chld_map.size();
		default: abort(); // unreachable
		}
	}
	operator int() { return ev.num; }
	operator double() { return ev.num; }
	operator cstring() { return ev.str; }
	json& operator[](int n) { return chld_list[n]; }
	json& operator[](size_t n) { return chld_list[n]; }
	json& operator[](const char * s) { return chld_map.get_create(s); }
	json& operator[](cstring s) { return chld_map.get_create(s); }
};

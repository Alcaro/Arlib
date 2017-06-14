#pragma once
#include "global.h"
#include "array.h"
#include "string.h"
#include "stringconv.h"

//This is a streaming parser. It returns a sequence of event objects.
//For example, the document
/*
{ "foo": [ 1, 2, 3 ] }
*/
//would yield { enter_map } { map_key, "foo" } { enter_list } { num, 1 } { num, 2 } { num, 3 } { exit_list } { exit_map }
//The parser keeps trying after an { error }, giving you a partial view of the damaged document; however,
// there are no guarantees on how much you can see, and it is likely for one error to cause many more, or misplaced nodes.
//enter/exit types are always paired, even in the presense of errors.
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



//class jsonserialize_impl



class jsonunserialize_impl {
	jsonparser p;
	jsonparser::event ev;
	bool matchagain;
	
	jsonunserialize_impl(cstring json) : p(json) {}
	template<typename T> friend T jsonunserialize(cstring json);
	
	void finish_item()
	{
		if (ev.action == jsonparser::enter_map || ev.action == jsonparser::enter_list)
		{
			while (true)
			{
				ev = p.next();
				finish_item();
				if (ev.action == jsonparser::exit_map || ev.action == jsonparser::exit_list) break;
			}
		}
	}
	
#define LEAF(T) void read_item(T& out) { if (ev.action == jsonparser::num) out = ev.num; finish_item(); ev = p.next(); }
	ALLNUMS(LEAF);
#undef LEAF
	
	void read_item(string& out)
	{
		if (ev.action == jsonparser::str) out = ev.str;
		finish_item();
		ev = p.next();
	}
	
	template<typename T> void read_item(array<T>& out)
	{
		out.reset();
		if (ev.action == jsonparser::enter_list)
		{
			ev = p.next();
			while (ev.action != jsonparser::exit_list)
			{
				read_item(out.append());
			}
		}
		else finish_item();
		ev = p.next();
	}
	
	template<typename T> void read_item(set<T>& out)
	{
		out.reset();
		if (ev.action == jsonparser::enter_list)
		{
			ev = p.next();
			while (ev.action != jsonparser::exit_list)
			{
				T tmp;
				read_item(tmp);
				out.add(tmp);
			}
		}
		else finish_item();
		ev = p.next();
	}
	
	template<typename T> void read_item(T& out)
	{
		if (ev.action == jsonparser::enter_map)
		{
			ev = p.next();
			while (ev.action != jsonparser::exit_map)
			{
				matchagain = false;
				//ev = map_key
				out.serialize(*this);
				if (!matchagain)
				{
					ev = p.next(); // TODO: not very robust against broken documents
					//ev = enter_map or whatever
					finish_item();
					//ev = exit_map or whatever
					ev = p.next();
					//ev = map_key or exit_map
				}
//if(ev.action==jsonparser::finish)*(char*)0=0;
			}
		}
		else finish_item();
		ev = p.next();
	}
	
	
public:
	
	static const bool serializing = false;
	
	template<typename T> void item(cstring name, T& out)
	{
//puts(tostring(ev.action)+","+tostring(jsonparser::map_key)+" "+ev.str+","+name);
		//this should be a loop, in case of documents like '{ "foo": 1, "foo": 2, "foo": 3 }'
		while (ev.action == jsonparser::map_key && ev.str == name)
		{
			ev = p.next();
			read_item(out);
			matchagain = true;
//puts("::"+tostring(ev.action)+": "+tostring(jsonparser::map_key)+","+tostring(jsonparser::exit_map));
		}
	}
	
	template<typename T> typename std::enable_if<std::is_integral<T>::value>::type hex(cstring name, T& out)
	{
		while (ev.action == jsonparser::map_key && ev.str == name)
		{
			ev = p.next();
			if (ev.action == jsonparser::num) out = ev.num;
			finish_item();
			ev = p.next();
			matchagain = true;
		}
	}
	
	void hex(cstring name, arrayvieww<byte> out)
	{
		while (ev.action == jsonparser::map_key && ev.str == name)
		{
			ev = p.next();
			if (ev.action == jsonparser::str)
			{
				fromstringhex(ev.str, out);
			}
			finish_item();
			ev = p.next();
			matchagain = true;
		}
	}
	
	void hex(cstring name, array<byte>& out)
	{
		while (ev.action == jsonparser::map_key && ev.str == name)
		{
			ev = p.next();
			if (ev.action == jsonparser::str)
			{
				fromstringhex(ev.str, out);
			}
			finish_item();
			ev = p.next();
			matchagain = true;
		}
	}
	
	cstring next() const
	{
		if (ev.action == jsonparser::map_key) return ev.str;
		else return "";
	}
	
	void comment(cstring c) {}
};

template<typename T> T jsonunserialize(cstring json)
{
	T out{};
	jsonunserialize_impl s(json);
	s.ev = s.p.next();
	s.read_item(out);
	return out;
}

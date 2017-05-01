#pragma once
#include "global.h"
#include "bml.h"
#include "stringconv.h"
#include "set.h"

#define SERIALIZE_CORE(member) s.item(STR(member), member);
#define SERIALIZE(...) template<typename T> void serialize(T& s) { PPFOREACH(SERIALIZE_CORE, __VA_ARGS__); }

//Interface:
//class serializer {
//public:
//	static const bool serializing;
//	
//	//Valid types:
//	//- Any integral type ('char' doesn't count as integral)
//	//- string (but not cstring)
//	//- array, set, map (if their contents are serializable)
//	//    map must use integer or string key, no complex classes
//	//- Any object with a serialize() function (see below)
//	//The name can be any string.
//	template<typename T> void item(cstring name, T& item);
//	
//	//Similar to item(), uses hex rather than decimal if output is human readable (otherwise, identical to item).
//	//Valid types:
//	//- Any unsigned integral type
//	//- array<byte>, written as a hex string
//	//- arrayvieww<byte>; like above, but can be an rvalue
//	template<typename T> void hex(cstring name, T& item);
//	void hex(cstring name, arrayvieww<byte> item);
//	
//	//Like hex(), makes serialized data look nicer. Can be ignored.
//	void comment(cstring c);
//	
//	//Returns the next child name the structure expects to process. Valid only while unserializing.
//	cstring next();
//};
//
//struct serializable {
//	int a;
//	
//public:
//	template<typename T> // T is guaranteed to offer the serializer interface.
//	void serialize(T& s)
//	{
//		//If unserializing, this function can be called multiple (or zero) times if the document is
//		// corrupt. Don't change any state, only call the serializer functions.
//		//For strange cases, check s.serializing.
//		s.item("a", a);
//	}
//	//or (expands to the above)
//public:
//	SERIALIZE(a);
//};


class bmlserialize_impl {
	bmlwriter w;
	template<typename T> friend string bmlserialize(T& item);
	
	template<typename T> void add_node(cstring name, T& item)
	{
		w.enter(bmlwriter::escape(name), "");
		item.serialize(*this);
		w.exit();
	}
	
	template<typename T> void add_node(cstring name, array<T>& item)
	{
		for (auto& child : item)
		{
			add_node(name, child);
		}
	}
	
	template<typename T> void add_node(cstring name, set<T>& item)
	{
		for (auto const& child : item)
		{
			add_node(name, child);
		}
	}
	
	template<typename T> void add_node(cstring name, array<array<T>>& item) = delete;
	
#define LEAF(T) \
		void add_node(cstring name, T& item) { w.node(bmlwriter::escape(name), tostring(item)); } \
		void add_node(cstring name, const T& item) { w.node(bmlwriter::escape(name), tostring(item)); }
	ALLSTRINGABLE(LEAF);
#undef LEAF
	
public:
	
	static const bool serializing = true;
	
	void comment(cstring c)
	{
		w.comment(c);
	}
	
	template<typename T> void item(cstring name, T& item) { add_node(name, item); }
	
	template<typename T> void hex(cstring name, T& item)
	{
		w.node(bmlwriter::escape(name), tostringhex(item));
	}
	void hex(cstring name, arrayview<byte> item)
	{
		w.node(bmlwriter::escape(name), tostringhex(item));
	}
	
	cstring next() { abort(); } // illegal
};

template<typename T> string bmlserialize(T& item)
{
	bmlserialize_impl s;
	item.serialize(s);
	return s.w.finish();
}



class bmlunserialize_impl {
	bmlparser p;
	int pdepth = 0;
	
	int thisdepth = 0;
	cstring thisnode;
	cstring thisval;
	bool matchagain;
	
	bmlparser::event event()
	{
		bmlparser::event ret = p.next();
		if (ret.action == bmlparser::enter) pdepth++;
		if (ret.action == bmlparser::exit) pdepth--;
		if (ret.action == bmlparser::finish) pdepth=-2;
		return ret;
	}
	
	void skipchildren()
	{
		while (pdepth > thisdepth) event();
	}
	
	bmlunserialize_impl(cstring bml) : p(bml) {}
	template<typename T> friend T bmlunserialize(cstring bml);
	
	template<typename T> void read_item(T& out)
	{
		while (pdepth >= thisdepth)
		{
			bmlparser::event ev = event();
			if (ev.action == bmlparser::enter)
			{
				thisdepth++;
				thisnode = bmlwriter::unescape(ev.name);
				thisval = ev.value;
				do {
					matchagain = false;
					out.serialize(*this);
				} while (matchagain);
				thisdepth--;
				skipchildren();
			}
		}
	}
	
	template<typename T> void read_item(array<T>& out)
	{
		read_item(out.append());
	}
	
	template<typename T> void read_item(set<T>& out)
	{
		T tmp;
		read_item(tmp);
		out.add(tmp);
	}
	
	template<typename T> void read_item(cstring name, array<array<T>>& item) = delete;
	
	void to_next()
	{
		matchagain = false;
		
		if (pdepth >= thisdepth)
		{
			thisdepth--;
			skipchildren();
			
			bmlparser::event ev = event();
			if (ev.action == bmlparser::enter)
			{
				matchagain = true;
				thisnode = bmlwriter::unescape(ev.name);
				thisval = ev.value;
			}
			
			thisdepth++;
		}
	}
	
#define LEAF(T) void read_item(T& out) { fromstring(thisval, out); }
	ALLSTRINGABLE(LEAF);
#undef LEAF
	
public:
	
	static const bool serializing = false;
	
	template<typename T> void item(cstring name, T& out)
	{
		while (thisnode == name) // this should be a loop, in case of documents like 'foo bar=1 bar=2 bar=3'
		{
			read_item(out);
			thisnode = "";
			to_next();
		}
	}
	
	template<typename T> void hex(cstring name, T& out)
	{
		while (thisnode == name)
		{
			fromstringhex(thisval, out);
			thisnode = "";
			to_next();
		}
	}
	
	void hex(cstring name, arrayvieww<byte> out)
	{
		while (thisnode == name)
		{
			fromstringhex(thisval, out);
			thisnode = "";
			to_next();
		}
	}
	
	cstring next() { return thisnode; }
	cstring next_esc() { return thisnode; }
	
	void comment(cstring c) {}
};

template<typename T> T bmlunserialize(cstring bml)
{
	T out{};
	bmlunserialize_impl s(bml);
	s.read_item(out);
	return out;
}

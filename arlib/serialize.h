#pragma once
#include "global.h"
#include "bml.h"
#include "stringconv.h"

#define SERIALIZE_CORE(member) s.item(STR(member), member);
#define SERIALIZE(...) template<typename T> void serialize(T& s) { PPFOREACH(SERIALIZE_CORE, __VA_ARGS__); }

//Interface:
//class serializer {
//public:
//	static const bool serializing;
//	
//	//If the name isn't a valid BML node name (alphanumerics, dash and period only), undefined behavior.
//	//Valid types:
//	//- Any integral type ('char' doesn't count as integral)
//	//- string (but not cstring)
//	//- Any serializable object (see below)
//	//- Any array of the above (but no nested arrays)
//	template<typename T> void item(cstring name, T& item);
//	
//	//Similar to item().
//	//Valid types:
//	//- Any unsigned integral type; if applicable, may be written as hex rather than decimal (not necessarily applicable)
//	//- array<byte>; if applicable, written as a hex string (again, not necessarily applicable)
//	//- arrayvieww<byte>; like above, but can be an rvalue
//	template<typename T> void hex(cstring name, T& item);
//	
//	//Like hex(), makes serialized data look nicer. Can be ignored.
//	void comment(cstring c);
//};
//
//struct serializable {
//	int a;
//	
//	template<typename T> // T is guaranteed to offer the serializer interface.
//	void serialize(T& s)
//	{
//		//If unserializing, this function can be called multiple times. Don't do anything const-incorrect.
//		//For strange cases, check s.serializing().
//		//If the name isn't a valid BML node name, undefined behavior. Alphanumerics, dash and period only.
//		s("a", a);
//	}
//	//or (expands to the above)
//	SERIALIZE(a);
//};


class bmlserialize_impl {
	bmlwriter w;
	template<typename T> friend string bmlserialize(T& item);
	
	template<typename T> void add_node(cstring name, T& item)
	{
		w.enter(name, "");
		item.serialize(*this);
		w.exit();
	}
	
	template<typename T> void add_node(cstring name, array<T>& item)
	{
		for (size_t i=0;i<item.size();i++)
		{
			add_node(name, item[i]);
		}
	}
	
	template<typename T> void add_node(cstring name, array<array<T>>& item) = delete;
	
#define LEAF(T) void add_node(cstring name, T& item) { w.node(name, tostring(item)); }
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
		w.node(name, tostringhex(item));
	}
	void hex(cstring name, arrayview<byte> item)
	{
		w.node(name, tostringhex(item));
	}
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
				thisnode = ev.name;
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
	
	template<typename T> void read_item(cstring name, array<array<T>>& item) = delete;
	
	void next()
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
				thisnode = ev.name;
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
			next();
		}
	}
	
	template<typename T> void hex(cstring name, T& out)
	{
		while (thisnode == name) // this should be a loop, in case of documents like 'foo bar=1 bar=2 bar=3'
		{
			fromstringhex(thisval, out);
			thisnode = "";
			next();
		}
	}
	
	void hex(cstring name, arrayvieww<byte> out)
	{
		while (thisnode == name) // this should be a loop, in case of documents like 'foo bar=1 bar=2 bar=3'
		{
			fromstringhex(thisval, out);
			thisnode = "";
			next();
		}
	}
	
	void comment(cstring c) {}
};

template<typename T> T bmlunserialize(cstring bml)
{
	T out{};
	bmlunserialize_impl s(bml);
	s.read_item(out);
	return out;
}

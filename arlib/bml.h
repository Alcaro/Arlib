#pragma once
#include "global.h"
#include "array.h"
#include "string.h"
#include "serialize.h"


//This is a streaming parser. For each node, { enter } then { exit } is returned; more enter/exit pairs may be present between them.
//For example, the document
/*
parent child=1
parent2
*/
//would yield { enter, parent, "" }, { enter, child, 1 }, { exit } { exit } { enter, parent2, "" } { exit }.
//The parser keeps trying after an { error }, giving you a partial view of the damaged document; however,
// there are no guarantees on how much you can see, and it is likely for one error to cause many more, or misplaced nodes.
//enter/exit is always paired, even in the presense of errors.
//After the document ends, { finish } will be returned forever until the object is deleted.
class bmlparser : nocopy {
public:
	enum { enter, exit, error, finish };
	struct event {
		int action;
		cstring name;
		cstring value; // or error message
	};
	
	//Since this takes a cstring, the string must be kept alive until the object is disposed.
	bmlparser(cstring bml) : m_orig_data(bml), m_data(bml), m_exit(false) {}
	event next();
	
private:
	cstring m_orig_data; // keep a reference if we're passed in the only copy of a string object
	cstring m_data;
	cstring m_thisline;
	array<bool> m_indent_step;
	cstring m_indent;
	cstring m_inlines;
	bool m_exit;
	
	inline void getlineraw();
	inline bool getline();
};

//This is also streaming. It may disobey the mode if the value is not supported; for example, val!="" on mode=anon won't work.
//It also disobeys mode <= inl_col on enter(), you need node() for that.
//Calling exit() without a matching enter(), or finish() without closing every enter(), is undefined behavior.
class bmlwriter {
	string m_data;
	int m_indent = 0;
	bool m_caninline = false;
	
	string indent()
	{
		string ret;
		char* ptr = ret.construct(m_indent*2);
		memset(ptr, ' ', m_indent*2);
		return ret;
	}
	
public:
	enum mode {
		ianon,   // parent node
		ieq,     // parent node=value
		iquote,  // parent node="value"
		icol,    // parent node: value
		
		anon,    // node
		eq,      // node=value
		quote,   // node="value"
		col,     // node: value
		
		multiline // node\n  :value
	};
	
	void enter(cstring name, cstring val, mode m = anon); // Since this implies the tag has children, it can't use the inline modes.
	void exit();
	void linebreak();
	void comment(cstring text);
	void node(cstring name, cstring val, mode m = ianon);
	
	//Tells what mode will actually be used if node() is called with these parameters and in this context.
	mode typeof(cstring val, mode m) const;
private:
	
	void node(cstring name, cstring val, mode m, bool enter);
	static mode typeof_core(cstring val);
public:
	
	string finish() { return m_data; }
};

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
//would yield { enter, parent, "" }, { enter, child, 1 }, { exit } { exit } { enter, "parent2", "" } { exit }.
//The parser keeps trying after an { error }, giving you a partial view of the damaged document; however,
// there are no guarantees on how much you can see, and it is likely for one error to cause many more.
//enter/exit is always paired, even in the presense of errors.
//After the document ends, { finish } will be returned forever until the object is deleted.
class bmlparser {
public:
	enum { enter, exit, error, finish };
	struct event {
		int action;
		cstring name;
		cstring value; // or error message
	};
	
	virtual event next() = 0;
	virtual ~bmlparser() {}
	
	//Since this takes a cstring, the string must be kept alive until the object is disposed.
	static bmlparser* create(cstring bml);
};

//This is also streaming. It may disobey the mode if the value is not supported; for example, val!="" on bml_anon won't help you.
//It also disobeys mode <= bml_inl_col on enter(), you need node().
//Calling exit() without a matching enter(), or finish() without closing every enter(), is undefined behavior.
class bmlwriter {
public:
	enum mode {
		anon,     // parent node
		inl_eq,   // parent node=value
		inl_col,  // parent node: value
		eq,       // node=value
		col,      // node: value
		multiline // node\n  :value
	};
	
	virtual void enter(cstring name, cstring val, mode m) = 0;
	virtual void exit() = 0;
	virtual void linebreak() = 0;
	virtual void comment(cstring text) = 0;
	virtual void node(cstring name, cstring val, mode m) = 0;
	
	//This deletes the object and returns the resulting string.
	virtual string finish() = 0;
	
	static bmlwriter* create();
private:
	virtual ~bmlwriter() {}
};

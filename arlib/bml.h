#pragma once
#include "global.h"
#include "array.h"
#include "string.h"
#include "serialize.h"

//This is a streaming parser. For each node, enter() then exit() is called; more enter/exit pairs may be present between the calls.
//For example, the document
/*
parent
  node=1 child=2
parent2
*/
//would yield enter("parent", "") enter("node", "1") enter("child", "2") exit() exit() exit() enter("parent2", "") exit()
//error() is called if the document is somehow invalid. However, it tries to parse the remainder anyways. It can be called multiple times.
//Return value is whether the document is error-free (whether error() was not called).
class bmlparser {
public:
	virtual void enter(cstring name, cstring val) = 0;
	virtual void exit() = 0;
	virtual void error(cstring what) {}
};
bool bml_parse(cstring bml, bmlparser* ret);

//This is also streaming. It may disobey the mode if the value is not supported; for example, val!="" on bml_anon won't help you.
//It also disobeys mode <= bml_inl_col on enter(), you need node().
//Calling exit() without a matching enter(), or finish() without closing every enter(), is undefined behavior.
enum bml_t {
	bml_anon,     // parent node
	bml_inl_eq,   // parent node=value
	bml_inl_col,  // parent node: value
	bml_eq,       // node=value
	bml_col,      // node: value
	bml_multiline // node\n  :value
};
class bmlwriter {
public:
	virtual void enter(cstring name, cstring val, bml_t mode) = 0;
	virtual void exit() = 0;
	virtual void linebreak() = 0;
	virtual void comment(cstring text) = 0;
	virtual void node(cstring name, cstring val, bml_t mode) = 0;
	
	//This deletes the object and returns the resulting string.
	virtual string finish() = 0;
private:
	virtual ~bmlwriter() {}
};
bmlwriter* bml_create();

#pragma once
#include "global.h"
#include "array.h"
#include "string.h"

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

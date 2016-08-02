#include "serialize.h"
#include "test.h"
#include <string.h>

/*

{ a; b; }
a=1
b=2

{ a { b; c; } d; }
a
  b=1
  c=2
d=3

*/

#ifdef ARLIB_TEST
#include "bml.h"

test()
{
	
	return true;
}
#endif

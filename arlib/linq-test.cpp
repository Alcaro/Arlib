#include "array.h"
#include "set.h"
#include "linq.h"
#include "test.h"

#ifdef ARLIB_TEST
test()
{
	{
		array<int> x = { 1, 2, 3 };
		array<short> y = x.select([&](int n) -> short { return n+x.size(); });
		assert_eq(y.size(), 3);
		assert_eq(y[0], 4);
		assert_eq(y[1], 5);
		assert_eq(y[2], 6);
	}
	
	{
		set<int> x = { 1, 2, 3 };
		set<short> y = x.select([&](int n) -> short { return n+x.size(); });
		assert_eq(y.size(), 3);
		assert(y.contains(4));
		assert(y.contains(5));
		assert(y.contains(6));
	}
	
	{
		array<int> x = { 1, 2, 3 };
		int i = 1;
		for (int n : x.select([](int n) -> short { return n*2; }))
		{
			assert_eq(n, i*2);
			i++;
		}
		assert_eq(i, 4);
	}
}
#endif

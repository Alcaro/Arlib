#include "set.h"
#include "test.h"

#ifdef ARLIB_TEST
test("set")
{
	{
		set<int> item;
		uint32_t set_flags;
		
		assert(!item.contains(1));
		assert(!item.contains(2));
		
		set_flags = 0;
		for (int m : item) { assert(!(set_flags & (1<<m))); set_flags |= 1<<m; }
		assert_eq(set_flags, 0);
		
		item.add(1);
		assert(item.contains(1));
		assert(!item.contains(2));
		
		set_flags = 0;
		for (int m : item) { assert(!(set_flags & (1<<m))); set_flags |= 1<<m; }
		assert_eq(set_flags, 1<<1);
		
		item.add(2);
		assert(item.contains(1));
		assert(item.contains(2));
		
		set_flags = 0;
		for (int m : item) { assert(!(set_flags & (1<<m))); set_flags |= 1<<m; }
		assert_eq(set_flags, 1<<1 | 1<<2);
		
		item.remove(1);
		assert(!item.contains(1));
		assert(item.contains(2));
		
		set_flags = 0;
		for (int m : item) { assert(!(set_flags & (1<<m))); set_flags |= 1<<m; }
		assert_eq(set_flags, 1<<2);
	}
	
	{
		set<string> item;
		
		assert(!item.contains("foo"));
		assert(!item.contains("bar"));
		assert(!item.contains("baz"));
		
		item.add("foo");
		assert(item.contains("foo"));
		assert(!item.contains("bar"));
		assert(!item.contains("baz"));
		
		item.add("bar");
		assert(item.contains("foo"));
		assert(item.contains("bar"));
		assert(!item.contains("baz"));
		
		item.remove("foo");
		assert(!item.contains("foo"));
		assert(item.contains("bar"));
		assert(!item.contains("baz"));
		
		item.remove("baz");
		assert(!item.contains("foo"));
		assert(item.contains("bar"));
		assert(!item.contains("baz"));
	}
	
	{
		for (int i=0;i<32;i++)
		{
			set<int> item;
			for (int j=0;j<i;j++)
			{
				for (int k=0;k<=j;k++)
				{
					item.add(j); // add the same thing multiple times for no reason
				}
			}
			
			assert_eq(item.size(), i);
			
			for (int j=0;j<32;j++)
			{
				if (j<i) assert(item.contains(j));
				else assert(!item.contains(j));
			}
		}
	}
	
	{
		set<string> item;
		
		item.add("a");
		item.add("b");
		item.add("c");
		item.add("d");
		item.add("e");
		
		uint32_t set_flags = 0;
		size_t len_iter = 0;
		for (auto& x : item)
		{
			if (x.length()==1 && x[0]>='a' && x[0]<='z')
			{
				uint32_t flag = 1<<(x[0]-'a');
				assert_eq(set_flags&flag, 0);
				set_flags |= flag;
			}
			len_iter++;
			assert(len_iter < 32);
		}
		assert_eq(len_iter, item.size());
		assert_eq(set_flags, (1<<item.size())-1);
	}
	
	{
		class unhashable {
			int x;
		public:
			unhashable(int x) : x(x) {}
			size_t hash() { return 0; }
			bool operator==(const unhashable& other) { return x==other.x; }
		};
		
		set<unhashable> item;
		
		for (int i=0;i<256;i++) item.add(unhashable(i));
		
		//test passes if it does not enter an infinite loop of trying the same 4 slots over and
		// over, when the other 4 are unused
	}
	
	{
		set<int> item;
		
		for (int i=0;i<256;i++)
		{
			item.add(i);
			item.remove(i);
		}
		
		//test passes if it does not enter an infinite loop of looking for a 'nothing more to see
		// here' slot when all slots are 'there was something here, but keep looking'
	}
}

test("map")
{
	{
		map<int,int> item;
		uint32_t set_flags;
		
		assert(!item.contains(1));
		assert(!item.contains(2));
		
		set_flags = 0;
		for (auto& p : item) { int m = p.key; assert_eq(p.value, p.key+2); assert(!(set_flags & (1<<m))); set_flags |= 1<<m; }
		assert_eq(set_flags, 0);
		
		item.insert(1, 3);
		assert(item.contains(1));
		assert(!item.contains(2));
		assert_eq(item.get(1), 3);
		
		set_flags = 0;
		for (auto& p : item) { int m = p.key; assert_eq(p.value, p.key+2); assert(!(set_flags & (1<<m))); set_flags |= 1<<m; }
		assert_eq(set_flags, 1<<1);
		
		item.insert(2, 4);
		assert(item.contains(1));
		assert(item.contains(2));
		assert_eq(item.get(1), 3);
		assert_eq(item.get(2), 4);
		
		set_flags = 0;
		for (auto& p : item) { int m = p.key; assert_eq(p.value, p.key+2); assert(!(set_flags & (1<<m))); set_flags |= 1<<m; }
		assert_eq(set_flags, 1<<1 | 1<<2);
		
		item.remove(1);
		assert(!item.contains(1));
		assert(item.contains(2));
		assert_eq(item.get(2), 4);
		
		set_flags = 0;
		for (auto& p : item) { int m = p.key; assert_eq(p.value, p.key+2); assert(!(set_flags & (1<<m))); set_flags |= 1<<m; }
		assert_eq(set_flags, 1<<2);
	}
	
	{
		map<string,string> item;
		
		assert(!item.contains("foo"));
		assert(!item.contains("bar"));
		assert(!item.contains("baz"));
		
		item.insert("foo", "Foo");
		assert(item.contains("foo"));
		assert(!item.contains("bar"));
		assert(!item.contains("baz"));
		assert_eq(item.get("foo"), "Foo");
		
		item.insert("bar", "Bar");
		assert(item.contains("foo"));
		assert(item.contains("bar"));
		assert(!item.contains("baz"));
		assert_eq(item.get("foo"), "Foo");
		assert_eq(item.get("bar"), "Bar");
		
		item.remove("foo");
		assert(!item.contains("foo"));
		assert(item.contains("bar"));
		assert(!item.contains("baz"));
		assert_eq(item.get("bar"), "Bar");
		
		item.remove("baz");
		assert(!item.contains("foo"));
		assert(item.contains("bar"));
		assert(!item.contains("baz"));
		assert_eq(item.get("bar"), "Bar");
	}
}
#endif
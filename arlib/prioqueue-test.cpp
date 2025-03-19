#include "prioqueue.h"
#include "test.h"

#ifdef ARLIB_TEST
template<typename T>
static void validate(const prioqueue<T>& q)
{
	arrayview<T> items = q.peek_heap();
	for (size_t i=0;i<items.size();i++)
	{
		if (i*2+1 < items.size())
			assert_lte(items[i], items[i*2+1]);
		if (i*2+2 < items.size())
			assert_lte(items[i], items[i*2+2]);
	}
}

template<typename T>
static void test_queue(array<T> items)
{
	prioqueue<T> q;
	for (const T& i : items)
	{
		q.push(i);
		validate(q);
	}
	
	array<T> extracted;
	while (q.size())
	{
		extracted.append(q.pop());
		validate(q);
	}
	
	prioqueue<T> q2 = items;
	array<T> extracted2;
	validate(q2);
	while (q2.size())
	{
		extracted2.append(q2.pop());
		validate(q2);
	}
	
	items.ssort();
	assert_eq(tostring_dbg(extracted), tostring_dbg(items));
	assert_eq(tostring_dbg(extracted2), tostring_dbg(items));
}

template<typename T>
static void test_queue_poppush(array<T> a, array<T> b, array<T> c)
{
	prioqueue<T> q = a;
	array<T> extracted;
	for (T& it : b)
	{
		extracted.append(q.poppush(it));
		validate(q);
	}
	while (q.size())
	{
		extracted.append(q.pop());
		validate(q);
	}
	assert_eq(tostring_dbg(extracted), tostring_dbg(c));
}

test("priority queue", "array", "prioqueue")
{
	test_queue<int>({});
	test_queue<int>({ 1 });
	test_queue<int>({ 1, 2 });
	test_queue<int>({ 2, 1 });
	test_queue<int>({ 0,1,2,3,4,5,6,7,8,9 });
	test_queue<int>({ 9,8,7,6,5,4,3,2,1,0 });
	test_queue<int>({ 3,1,4,5,9,2,6,8,7,0 }); // the unique digits of pi, in order
	test_queue<int>({ 3,1,4,1,5,9,2,6,5,3 }); // a few duplicate elements
	test_queue<int>({ 0,1,7,2,5,8,9,3,4,6 }); // maximally unsorted heap; every child in the left tree is smaller than the right child
	test_queue<int>({ 0,4,1,7,5,3,2,9,8,6 }); // the above but mirrored; every child in the right tree is smaller than the left child
	test_queue<int>({ 0,1,2,3,4,5,6,7,8 }); // the above six, minus the last element
	test_queue<int>({ 9,8,7,6,5,4,3,2,1 });
	test_queue<int>({ 3,1,4,5,9,2,6,8,7 });
	test_queue<int>({ 3,1,4,1,5,9,2,6,5 });
	test_queue<int>({ 0,1,7,2,5,8,9,3,4 });
	test_queue<int>({ 0,4,1,7,5,3,2,9,8 });
	
	test_queue_poppush<int>({ 3,1,4,1,5 }, { 9,2,6,5,3,5,8,9,7,9,3,2,3,8,4 },
	                                     { 1,1,2,3,4,3,5,5,5,6,7,3,2,3,8,4,8,9,9,9 });
	
	class nontrivial {
		int* body;
	public:
		nontrivial(int n) { body = new int(n); }
		nontrivial(const nontrivial& other) { body = new int(other.body[0]); }
		~nontrivial() { delete body; }
		bool operator<(const nontrivial& other) const { return *body < other.body[0]; }
		operator string() const { return tostring(*body); }
	};
	test_queue<nontrivial>({ 3,1,4,5,9,2,6,8,7,0 });
}
#endif

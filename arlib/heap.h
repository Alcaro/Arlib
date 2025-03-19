#pragma once
#include "global.h"

class heap {
	// the heap invariant: items[n] <= items[n*2+1] && items[n] <= items[n*2+2]
	static size_t child1(size_t n) { return n*2+1; }
	static size_t child2(size_t n) { return n*2+2; }
	static size_t parent(size_t n) { return (n-1)/2; }
	
	template<typename T>
	static void move(T* dst, T* src)
	{
		memcpy((void*)dst, (void*)src, sizeof(T));
	}
	template<typename T>
	static void move(T* ptr, size_t dst, size_t src)
	{
		move(ptr+dst, ptr+src);
	}
	
	template<typename T>
	static void swap(T* a, T* b)
	{
		char tmp[sizeof(T)];
		memcpy(tmp, (void*)a, sizeof(T));
		memcpy((void*)a, (void*)b, sizeof(T));
		memcpy((void*)b, tmp, sizeof(T));
	}
	template<typename T>
	static void swap(T* ptr, size_t dst, size_t src)
	{
		swap(ptr+dst, ptr+src);
	}
	
	// given an almost-heap, where 'elem' may need to be pushed down, turns it into a real heap
	// - find the smaller (or only) child
	// - compare to the parent
	// - if larger, swap, and repeat these steps for the now-child
	template<typename T, typename Tless>
	static void push_down(T* body, size_t elem, size_t len, const Tless& less)
	{
		if (child2(elem) >= len)
		{
			if (child1(elem) < len)
			{
				if (less(body[child1(elem)], body[elem]))
					swap(body, child1(elem), elem);
			}
			return;
		}
		size_t par = elem;
		size_t ch1 = child1(elem);
		size_t ch2 = child2(elem);
		size_t min_ch = (less(body[ch1], body[ch2]) ? ch1 : ch2);
		if (less(body[min_ch], body[par]))
		{
			swap(body, min_ch, par);
			push_down(body, min_ch, len, less);
		}
	}
	
public:
	template<typename T, typename Tless>
	static void heapify(T* body, size_t len, const Tless& less)
	{
		if (len <= 1)
			return;
		// - a single node is a heap
		// - given two heaps and an extra node, turning them into a bigger heap is simply placing the extra node as the root,
		//     then pushing it down until it's a heap
		// - the bottom half takes no time, nothing to do
		// - the bottom half of the remainder must be combined with the 1-heaps at the bottom to create 3-heaps
		// - the bottom half of the remainder must be combined with the 3-heaps to create 7-heaps
		// - etc
		// - a 3-heap is created with at most 1 swap
		// - a 7-heap is created with at most 2 swaps, plus the 2 in the children = 4
		// - a 15-heap is created with at most 3 swaps, plus the 8 in the children = 11
		// - a 31-heap is created with at most 4 swaps, plus the 22 in the children = 26
		// - this is equal to heap size minus heap height, and cannot go above heap size
		// - therefore, heap creation is O(n)
		size_t i = parent(len-1);
		while (true)
		{
			push_down(body, i, len, less);
			if (!i)
				break;
			i--;
		}
	}
	template<typename T>
	static void heapify(T* body, size_t len)
	{
		heapify(body, len, [](const T& a, const T& b) { return a < b; });
	}
	
	// Will memcpy new_elem to somewhere in body, and overwrite body[len].
	// It's caller's responsibility to not call new_elem's dtor afterwards.
	// Length must be the heap size before the operation. new_elem may not be at body[len].
	template<typename T, typename Tless>
	static void push(T* body, size_t len, T* new_elem, const Tless& less)
	{
		size_t new_pos = len;
		while (new_pos != 0 && less(*new_elem, body[parent(new_pos)]))
		{
			move(body, new_pos, parent(new_pos));
			new_pos = parent(new_pos);
		}
		move(body+new_pos, new_elem);
	}
	template<typename T>
	static void push(T* body, size_t len, T* new_elem)
	{
		push(body, len, new_elem, [](const T& a, const T& b) { return a < b; });
	}
	
	// Will memcpy something from body to ret, and leave body[len-1] as garbage that should not be destructed.
	// It's caller's responsibility to ensure ret is uninitialized prior to the call.
	// Length must be the heap size before the operation. ret may not be at body[len-1].
	template<typename T, typename Tless>
	static void pop(T* body, size_t len, T* ret, const Tless& less)
	{
		move(ret, body+0);
		size_t n_items = len-1;
		if (n_items)
		{
			size_t parent = 0;
			while (true)
			{
				size_t child = child1(parent);
				if (child >= n_items)
					break;
				if (child+1 < n_items && less(body[child+1], body[child]))
					child++;
				if (less(body[n_items], body[child]))
					break;
				move(body, parent, child);
				parent = child;
			}
			move(body, parent, n_items);
		}
	}
	template<typename T>
	static void pop(T* body, size_t len, T* ret)
	{
		pop(body, len, ret, [](const T& a, const T& b) { return a < b; });
	}
	
	// Combines the above two. ret may not be equal to new_elem.
	template<typename T, typename Tless>
	static void poppush(T* body, size_t len, T* ret, T* new_elem, const Tless& less)
	{
		move(ret, body);
		move(body, new_elem);
		push_down(body, 0, len, less);
	}
	template<typename T>
	static void poppush(T* body, size_t len, T* ret, T* new_elem)
	{
		poppush(body, len, ret, new_elem, [](const T& a, const T& b) { return a < b; });
	}
};

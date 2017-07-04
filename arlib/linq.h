//#include "global.h"

#ifndef LINQ_BASE_INCLUDED
#define LINQ_BASE_INCLUDED

template<typename T> class linqobj;
//'T' is whatever the container contains.
//'base' is the base class with .begin() and .end(), including template argument.
//For example: template<typename T> class arrayview : public linqbase<T, arrayview<T>>
template<typename T, typename base>
class linqbase : empty {
	base& impl() { return *(base*)this; }
	const base& impl() const { return *(base*)this; }
public:
	template<typename T3, typename T2 = typename std::result_of<T3(T)>::type>
	linqobj<T2> select(T3 conv) const
	{
		return linqobj<T>(impl()).select(conv);
	}
};
#endif


#ifndef LINQ_BASE
#pragma once
#include "array.h"
#include "set.h"

//This entire class is private. Do not store or create any instance, other than what the LINQ functions return.
template<typename T> class linqobj : nocopy {
public:
	//Acts roughly like a normal C++ iterator, but operator!= is replaced with valid(), and functions are named rather than operators.
	class iterator : nocopy {
	public:
		virtual bool valid() = 0;
		virtual void next() = 0;
		virtual T get() = 0;
		virtual ~iterator() {}
	};
	autoptr<iterator> it;
	
	linqobj(iterator* it) : it(it) {}
	linqobj(autoptr<iterator> it) : it(it) {}
	
	class it_arrayview : public iterator {
	public:
		const T* it;
		const T* end;
		it_arrayview(arrayview<T> list) : it(list.begin()), end(list.end()) {}
		bool valid() { return it != end; }
		void next() { ++it; }
		T get() { return *it; }
	};
	linqobj(arrayview<T> list)
	{
		it = new it_arrayview(list);
	}
	class it_set : public iterator {
	public:
		typename set<T>::iterator it;
		typename set<T>::iterator end;
		it_set(const set<T>& s) : it(s.begin()), end(s.end()) {}
		bool valid() { return it != end; }
		void next() { ++it; }
		T get() { return *it; }
	};
	linqobj(const set<T>& list)
	{
		it = new it_set(list);
	}
	
	template<typename Tin, typename Tconv>
	class it_select : public iterator {
	public:
		autoptr<typename linqobj<Tin>::iterator> base;
		Tconv conv;
		
		it_select(typename linqobj<Tin>::iterator* base, Tconv conv) : base(base), conv(conv) {}
		bool valid() { return base->valid(); }
		void next() { base->next(); }
		T get() { return conv(base->get()); }
	};
	template<typename T3, typename T2 = typename std::result_of<T3(T)>::type>
	linqobj<T2> select(T3 conv)
	{
		return new typename linqobj<T2>::template it_select<T, T3>(it.release(), conv);
	}
	
	operator array<T>()
	{
		array<T> ret;
		while (it->valid())
		{
			ret.append(it->get());
			it->next();
		}
		return ret;
	}
	
	operator set<T>()
	{
		set<T> ret;
		while (it->valid())
		{
			ret.add(it->get());
			it->next();
		}
		return ret;
	}
};

#endif
#undef LINQ_BASE

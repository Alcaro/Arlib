//#include "global.h"

#ifndef LINQ_BASE_INCLUDED
#define LINQ_BASE_INCLUDED

template<typename T, typename Titer> class linqobj;
//'T' is whatever the container contains.
//'Tbase' is the base class with .begin() and .end(), including template argument.
//'Titer' is the type of .begin().
//'Titer2' is the type of .end(), if different from .begin().
//For example: template<typename T> class arrayview : public linqbase<T, arrayview<T>, const T*>
template<typename T, typename Tbase>
class linqbase : empty {
	//doesn't exist, only used because the real impl() needs a 'this' and decltype doesn't have that
	//dummy template parameters are to ensure it doesn't refer to Tbase::begin() before Tbase is properly defined
	template<typename _> static Tbase& decltype_impl();
	
	Tbase& impl() { return *(Tbase*)this; }
	const Tbase& impl() const { return *(Tbase*)this; }
	
	template<typename _>
	linqobj<T, decltype(decltype_impl<_>().begin())> linq() const
	{
		return linqobj<T, decltype(impl().begin())>(impl().begin(), impl().end());
	}
public:
	template<typename T3, typename T2 = typename std::result_of<T3(T)>::type>
	auto select(T3 conv) const
	{
		return linq<void>().select(conv);
	}
};
#endif


#ifndef LINQ_BASE
#pragma once
#include "array.h"
#include "set.h"

//This entire class is private. Do not store or create any instance, other than what the LINQ functions return.
template<typename T, typename Titer> class linqobj : nocopy {
public:
	//TODO: split to two types when switching to c++17
	Titer b;
	Titer e;
	
	linqobj(Titer b, Titer e) : b(b), e(e) {}
	
	Titer begin() { return b; }
	Titer end() { return e; }
	
	
	template<typename Tout, typename Tconv>
	class i_select {
	public:
		Titer base;
		Tconv conv;
		
		i_select(Titer base, Tconv conv) : base(base), conv(conv) {}
		bool operator!=(const i_select other) { return base != other.base; }
		void operator++() { ++base; }
		Tout operator*() { return conv(*base); }
	};
	template<typename T3, typename T2 = typename std::result_of<T3(T)>::type>
	auto select(T3 conv)
	{
		return linqobj<T2, i_select<T2, T3>>(i_select<T2, T3>(std::move(b), conv),
		                                     i_select<T2, T3>(std::move(e), conv));
	}
	
	
	operator array<T>()
	{
		array<T> ret;
		for (auto&& item : *this) ret.append(item);
		return ret;
	}
	
	operator set<T>()
	{
		set<T> ret;
		for (auto&& item : *this) ret.add(item);
		return ret;
	}
};

#endif
#undef LINQ_BASE

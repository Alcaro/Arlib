//#include "global.h"

#ifndef LINQ_BASE_INCLUDED
#define LINQ_BASE_INCLUDED

namespace linq {
template<typename T, typename Titer> class t_base;
template<typename T, typename Titer, typename Tout, typename Tconv> class t_select;
}
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
	class alias {
	public:
		typedef decltype(decltype_impl<_>().begin()) iter;
		typedef linq::t_base<T, iter> linq_t;
	};
	
	template<typename _>
	typename alias<_>::linq_t as_linq() const
	{
		return typename alias<_>::linq_t(impl().begin(), impl().end());
	}
public:
	template<typename T3, typename T2 = typename std::result_of<T3(T)>::type>
	auto select(T3 conv) const -> linq::t_base<T2, linq::t_select<T, typename alias<T2>::iter, T2, T3>>
	{
		return as_linq<void>().select(conv);
	}
};
#endif


#ifndef LINQ_BASE
#pragma once
#include "array.h"
#include "set.h"

//This namespace is considered private. Do not store or create any instance, other than what the arrayview/set functions return.
namespace linq {
template<typename T, typename Titer, typename Tout, typename Tconv>
class t_select {
public:
	Titer base;
	Tconv conv;
	
	t_select(Titer base, Tconv conv) : base(base), conv(conv) {}
	bool operator!=(const t_select& other) { return base != other.base; }
	void operator++() { ++base; }
	Tout operator*() { return conv(*base); }
};

template<typename T, typename Titer> class t_base : nocopy {
public:
	//TODO: split to two types when switching to c++17
	Titer b;
	Titer e;
	
	t_base(Titer b, Titer e) : b(b), e(e) {}
	
	Titer begin() { return std::move(b); }
	Titer end() { return std::move(e); }
	
	template<typename T3, typename T2 = typename std::result_of<T3(T)>::type>
	auto select(T3 conv) -> t_base<T2, linq::t_select<T, Titer, T2, T3>>
	{
		return t_base<T2, linq::t_select<T, Titer, T2, T3>>(t_select<T, Titer, T2, T3>(std::move(b), conv),
		                                                    t_select<T, Titer, T2, T3>(std::move(e), conv));
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
}

#endif
#undef LINQ_BASE

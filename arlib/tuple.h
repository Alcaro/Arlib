#pragma once
#include "global.h"

template<typename Thead, typename... Ttail> class tuple_impl {
	template<typename...> friend class tuple;
	template<typename, typename...> friend class tuple_impl;
	
	Thead head;
	tuple_impl<Ttail...> tail;
	
	tuple_impl(Thead head, Ttail... tails) : head(head), tail(tails...) {}
};

template<typename Thead> class tuple_impl<Thead> {
	template<typename...> friend class tuple;
	template<typename, typename...> friend class tuple_impl;
	
	Thead head;
	tuple_impl(Thead head) : head(head) {}
};

template<typename... Ts> class tuple {
	tuple_impl<Ts...> m_data;
public:
	tuple(Ts... args) : m_data(args...) {}
	
	//template<size_t n>
	//T get() { return storage[n]; } // used by structured binding
};

template<typename T1, typename T2> class tuple<T1, T2> {
public:
	T1 first;
	T2 second;
	
	template<size_t N>
	std::conditional_t<N==0, T1&, T2&> get()
	{
		static_assert(N <= 1);
		if constexpr (N == 0) return first;
		else return second;
	}
};

template<> class tuple<> {};

template<typename... Ts>
struct std::tuple_size<tuple<Ts...>> { static constexpr size_t value = sizeof...(Ts); };
template<typename T1, typename T2, size_t N>
struct std::tuple_element<N, tuple<T1, T2>> { static_assert(N <= 1); using type = std::conditional_t<N==0, T1&, T2&>; };

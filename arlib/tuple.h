#pragma once
#include "global.h"

template<size_t N, typename T, typename... Ts>
struct nth_typename : nth_typename<N-1, Ts...> {};
template<typename T, typename... Ts>
struct nth_typename<0, T, Ts...> { using type = T; };

template<size_t N, typename T>
struct tuple_item { T val; };

template<typename... Ts> struct tuple_impl;
template<typename... Ts, size_t... Ns>
struct tuple_impl<std::index_sequence<Ns...>, Ts...> : tuple_item<Ns, Ts>... {
	tuple_impl() = default;
	tuple_impl(Ts&&... args) : tuple_item<Ns, Ts>{std::forward<decltype(args)>(args)}... {}
};

template<typename... Ts>
class tuple : tuple_impl<std::index_sequence_for<Ts...>, Ts...> {
public:
	using tuple_impl<std::index_sequence_for<Ts...>, Ts...>::tuple_impl;
	
	template<size_t N>
	auto& get()
	{
		static_assert(N < sizeof...(Ts));
		return tuple_item<N, typename nth_typename<N, Ts...>::type>::val;
	}
	
	template<size_t N>
	const auto& get() const
	{
		static_assert(N < sizeof...(Ts));
		return tuple_item<N, typename nth_typename<N, Ts...>::type>::val;
	}
};

template<typename... Ts>
struct std::tuple_size<tuple<Ts...>> { static constexpr size_t value = sizeof...(Ts); };
template<typename... Ts, size_t N>
struct std::tuple_element<N, tuple<Ts...>> : nth_typename<N, Ts...> {};

template<typename T1, typename T2>
class tuple<T1, T2> {
public:
	T1 first;
	T2 second;
	
	template<size_t N>
	std::conditional_t<N==0, T1&, T2&> get()
	{
		static_assert(N < 2);
		if constexpr (N == 0) return first;
		else return second;
	}
	
	template<size_t N>
	std::conditional_t<N==0, const T1&, const T2&> get() const
	{
		static_assert(N < 2);
		if constexpr (N == 0) return first;
		else return second;
	}
};

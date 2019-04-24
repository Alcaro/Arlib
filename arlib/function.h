#pragma once

//Inspired by
// http://www.codeproject.com/Articles/136799/ Lightweight Generic C++ Callbacks (or, Yet Another Delegate Implementation)
//but rewritten using C++11 features, to remove code duplication and 6-arg limits, and improve error messages.

#include <stddef.h>
#include <string.h>
#include <utility>

#ifdef __GNUC__
#define LIKELY(expr)    __builtin_expect(!!(expr), true)
#define UNLIKELY(expr)  __builtin_expect(!!(expr), false)
#else
#define LIKELY(expr)    (expr)
#define UNLIKELY(expr)  (expr)
#endif

template<typename T> class function;
template<typename Tr, typename... Ta>
class function<Tr(Ta...)> {
	typedef Tr(*Tfp)(void* ctx, Ta... args);
	typedef Tr(*Tfpr)(Ta... args);
	
	struct refcount
	{
		size_t count;
		void(*destruct)(void* ctx);
	};
	
	Tfp func;
	void* ctx;
	refcount* ref = NULL;
	
	class dummy {};
	
	static Tr empty(void* ctx, Ta... args) { return Tr(); }
	static Tr freewrap(void* ctx, Ta... args) { return ((Tfpr)ctx)(std::forward<Ta>(args)...); }
	
	void add_ref()
	{
		if (LIKELY(!ref)) return;
		ref->count++;
	}
	
	void unref()
	{
		if (LIKELY(!ref)) return;
		if (!--ref->count)
		{
			bool do_del = ((void*)ref != ctx);
			ref->destruct(ctx);
			if (do_del)
				delete ref;
		}
		ref = NULL;
	}
	
	void init_free(Tfpr fp)
	{
		if (fp)
		{
			func = freewrap;
			ctx = (void*)fp;
		}
		else
		{
			func = empty;
			ctx = (void*)empty;
		}
	}
	
	void init_ptr(Tfp fp, void* ctx)
	{
		this->func = fp;
		this->ctx = ctx;
	}
	
	template<typename Tl>
	typename std::enable_if< std::is_convertible<Tl, Tfpr>::value>::type
	init_lambda(Tl lambda)
	{
		init_free(lambda);
	}
	
	template<typename Tl>
	typename std::enable_if<!std::is_convertible<Tl, Tfpr>::value>::type
	init_lambda(Tl lambda)
	{
		if (std::is_trivially_move_constructible<Tl>::value &&
		    std::is_trivially_destructible<Tl>::value &&
		    sizeof(Tl) <= sizeof(void*))
		{
			void* obj = NULL;
			memcpy(&obj, &lambda, sizeof(lambda));
			
			auto wrap = [](void* ctx, Ta... args)
			{
				alignas(Tl) char l[sizeof(void*)];
				memcpy(l, &ctx, sizeof(Tl));
				return (*(Tl*)l)(std::forward<Ta>(args)...);
			};
			init_ptr(wrap, obj);
		}
		else
		{
			class holder {
				refcount rc;
				Tl l;
			public:
				holder(Tl l) : l(l) { rc.count = 1; rc.destruct = &holder::destruct; }
				static Tr call(holder* self, Ta... args) { return self->l(std::forward<Ta>(args)...); }
				static void destruct(void* self) { delete (holder*)self; }
			};
			this->func = (Tfp)&holder::call;
			this->ctx = new holder(std::move(lambda));
			this->ref = (refcount*)this->ctx;
		}
	}
	
public:
	function() { init_free(NULL); }
	function(const function& rhs) : func(rhs.func), ctx(rhs.ctx), ref(rhs.ref) { add_ref(); }
	function(function&& rhs)      : func(rhs.func), ctx(rhs.ctx), ref(rhs.ref) { rhs.ref = NULL; }
	function& operator=(const function& rhs)
		{ unref(); func = rhs.func; ctx = rhs.ctx; ref = rhs.ref; add_ref(); return *this; }
	function& operator=(function&& rhs)
		{ unref(); func = rhs.func; ctx = rhs.ctx; ref = rhs.ref; rhs.ref = NULL; return *this; }
	~function() { unref(); }
	
	function(Tfpr fp) { init_free(fp); } // function(NULL) hits here
	
	template<typename Tl>
	function(Tl lambda,
	         typename std::enable_if< // no is_invocable_r until c++17
	            std::is_convertible<decltype(std::declval<Tl>()(std::declval<Ta>()...)), Tr>::value
	         , dummy>::type ignore = dummy())
	{
		init_lambda(std::forward<Tl>(lambda));
	}
	
	template<typename Tl, typename Tc>
	function(Tl lambda,
	         Tc* ctx,
	         typename std::enable_if<
	            std::is_convertible<decltype(std::declval<Tl>()(std::declval<Tc*>(), std::declval<Ta>()...)), Tr>::value
	         , dummy>::type ignore = dummy())
	{
		this->func = (Tfp)(Tr(*)(Tc*, Ta...))lambda;
		this->ctx = (void*)ctx;
	}
	
	template<typename Tl, typename Tc>
	function(Tl lambda,
	         Tc* ctx,
	         void(*destruct)(void* ctx),
	         typename std::enable_if<
	            std::is_convertible<decltype(std::declval<Tl>()(std::declval<Tc*>(), std::declval<Ta>()...)), Tr>::value
	         , dummy>::type ignore = dummy())
	{
		this->func = (Tfp)(Tr(*)(Tc*, Ta...))lambda;
		this->ctx = (void*)ctx;
		this->ref = new refcount;
		this->ref->count = 1;
		this->ref->destruct = destruct;
	}
	
	Tr operator()(Ta... args) const { return func(ctx, std::forward<Ta>(args)...); }
private:
	//to make null objects callable, 'func' must be a valid function
	//I can not:
	//- use the lowest bits - requires mask at call time, and confuses the optimizer
	//- compare it to a static null function, I don't trust the compiler to merge it correctly
	//- use a function defined in a .cpp file - this is a single-header library and I want it to remain that way
	//nor can I use NULL/whatever in 'obj', because foreign code can find that argument just as easily as this one can
	//solution: set obj=func=empty for null functions
	//- empty doesn't use obj, it can be whatever
	//- it is not sensitive to false negatives - even if the address of empty changes, obj==func does not
	//- it is not sensitive to false positives - empty is private, and can't be aliased by anything unexpected
	//    (okay, it is sensitive on a pure Harvard architecture, but they're extinct and Modified Harvard is safe.)
	//- it is sensitive to hostile callers, but if you call bind_ptr(func, (void*)func), you're asking for trouble.
	bool isTrue() const
	{
		return ((void*)func != ctx);
	}
public:
	explicit operator bool() const { return isTrue(); }
	bool operator!() const { return !isTrue(); }
	
	//Splits a function into a function pointer and a context argument.
	//Calling the pointer, with the context as first argument, is equivalent to calling the function object
	// (modulo a few move constructor calls).
	//WARNING: Explodes if the function object owns any memory.
	//This happens if it refers to a lambda binding more than sizeof(void*) bytes, or if it's created with bind_ptr_del.
	//In typical cases (a lambda binding 'this' or nothing, or a member function), this is safe.
	void decompose(Tfp* func, void** ctx)
	{
		if (ref) abort();
		*func = this->func;
		*ctx = this->ctx;
	}
	
	//WARNING: If the function object owns memory, it must remain alive during any call to func/ctx.
	//If it requires this guarantee, it returns false; true means safe.
	bool try_decompose(Tfp* func, void** ctx)
	{
		*func = this->func;
		*ctx = this->ctx;
		return !ref;
	}
	
	//TODO: reenable, and add a bunch of static asserts that every type (including return) is either unchanged,
	// or a primitive type (integer, float or pointer - no structs or funny stuff) of the same size as the original
	//Usage: function<void(void*)> x; function<void(int*)> y = x.reinterpret<void(int*)>();
	//template<typename T>
	//function<T> reinterpret()
	//{
	//	
	//}
};

//std::function can work without this extra class in C++11, but only by doing another level of indirection at runtime
//C++17 template<auto fn> could do it without lamehacks, except still needs to get 'this' from somewhere
//callers should prefer a lambda with a [this] capture
template<typename Tc, typename Tr, typename... Ta>
class memb_rewrap {
public:
	template<Tr(Tc::*fn)(Ta...)>
	function<Tr(Ta...)> get(Tc* ctx)
	{
		return {
			[](Tc* ctx, Ta... args)->Tr { return (ctx->*fn)(std::forward<Ta>(args)...); },
			ctx };
	}
	
	template<Tr(Tc::*fn)(Ta...)>
	function<Tr(Ta...)> get_del(Tc* ctx)
	{
		return {
			[](Tc* ctx, Ta... args)->Tr { return (ctx->*fn)(std::forward<Ta>(args)...); },
			ctx,
			[](void* ctx) { delete (Tc*)ctx; } };
	}
};
template<typename Tc, typename Tr, typename... Ta>
memb_rewrap<Tc, Tr, Ta...>
fn_wrap(Tr(Tc::*)(Ta...))
{
	return memb_rewrap<Tc, Tr, Ta...>();
}

template<typename Tc, typename Tr, typename... Ta>
class memb_rewrap_const {
public:
	template<Tr(Tc::*fn)(Ta...) const>
	function<Tr(Ta...)> get(const Tc* ctx)
	{
		return {
			[](const Tc* ctx, Ta... args)->Tr { return (ctx->*fn)(std::forward<Ta>(args)...); },
			ctx };
	}
	
	template<Tr(Tc::*fn)(Ta...) const>
	function<Tr(Ta...)> get_del(const Tc* ctx)
	{
		return {
			[](const Tc* ctx, Ta... args)->Tr { return (ctx->*fn)(std::forward<Ta>(args)...); },
			ctx,
			[](void* ctx) { delete (Tc*)ctx; } };
	}
};
template<typename Tc, typename Tr, typename... Ta>
memb_rewrap_const<Tc, Tr, Ta...>
fn_wrap(Tr(Tc::*)(Ta...) const)
{
	return memb_rewrap_const<Tc, Tr, Ta...>();
}

//#define bind_free(fn)
#define bind_ptr(fn, ptr) (fn_wrap(fn).get<fn>(ptr)) // can't replace with fn_wrap<decltype(fn), fn>(ptr), what would it return?
#define bind_ptr_del(fn, ptr) (fn_wrap(fn).get_del<fn>(ptr))
//#define bind_ptr_del_exp(fn, ptr, destructor)
#define bind_this(fn) bind_ptr(fn, this) // reminder: bind_this(&classname::function), not bind_this(function)
//#define bind_this_del(fn) bind_ptr_del(fn, this)
//#define bind_this_del_exp(fn, destructor) bind_ptr_del_exp(fn, destructor, this)

//while the function template can be constructed from a lambda, I want bind_lambda(...).decompose(...) to work
//so just #define bind_lambda(...) { __VA_ARGS__ } is insufficient
template<typename Tl, typename Tr, typename... Ta>
function<Tr(Ta...)> bind_lambda_core(Tl&& l, Tr (Tl::*f)(Ta...) const)
{
	return l;
}
template<typename Tl, typename Tr, typename... Ta> // overload for mutable lambdas
function<Tr(Ta...)> bind_lambda_core(Tl&& l, Tr (Tl::*f)(Ta...))
{
	return l;
}
template<typename Tl>
decltype(bind_lambda_core<Tl>(std::declval<Tl>(), &Tl::operator()))
bind_lambda(Tl&& l)
{
	return bind_lambda_core<Tl>(std::move(l), &Tl::operator());
}

// Includes. {{{
#include "webobject.hh"
#include <coroutine>
#include <string>
#include <iostream>
#include <vector>
#include <map>
#include <cassert>
#include <endian.h>
#include <ieee754.h>
#include <sstream>
#include <memory>
// }}}

struct coroutine { // {{{
	struct promise_type;
	using handle_type = std::coroutine_handle <promise_type>;
	struct promise_type {
		// from_coroutine = handle(to_coroutine); to_coroutine = Yield(from_coroutine);
		std::shared_ptr <WebObject> from_coroutine;
		std::shared_ptr <WebObject> to_coroutine;

		// Callback is used for launched coroutines. It should not be set for YieldFrom.
		typedef void (*Cb)(std::shared_ptr <WebObject> ret, void *user_data);
		Cb cb;
		void *user_data;

		// Constructor and related functions.
		promise_type() : from_coroutine(), to_coroutine(), cb(nullptr) { STARTFUNC; }
		~promise_type() { STARTFUNC; }
		coroutine get_return_object() { STARTFUNC; return coroutine(handle_type::from_promise(*this)); }
		std::suspend_always initial_suspend() noexcept { STARTFUNC; return {}; }
		std::suspend_always final_suspend() noexcept { STARTFUNC; return {}; }
		void unhandled_exception() { STARTFUNC; }

		// If co_yield is used, this function is called. If Yield is used, the YieldAwaiter class below is used.
		std::suspend_always yield_value(std::shared_ptr <WebObject> v) { STARTFUNC; from_coroutine.swap(v); return {}; }

		// co_return can use the callback function, or return the value when used with YieldFrom.
		std::suspend_always return_value(std::shared_ptr <WebObject> v) { STARTFUNC; from_coroutine.swap(v); if (cb) cb(from_coroutine, user_data); return {}; }
		void set_cb(Cb new_cb, void *data = nullptr) { STARTFUNC; cb = new_cb; user_data = data; }
	};
	handle_type handle;
	coroutine(handle_type handle) : handle(handle) { STARTFUNC; }
	coroutine(coroutine const &other) = delete;
	coroutine &operator=(coroutine const &other) = delete;
	~coroutine() { STARTFUNC; handle.destroy(); }
	std::shared_ptr <WebObject> operator()(std::shared_ptr <WebObject> to_coroutine = WebNone::create());	// Argument is returned from Yield.
	operator bool() { STARTFUNC; return handle.done(); }
	void set_cb(promise_type::Cb new_cb, void *data = nullptr) { STARTFUNC; handle.promise().set_cb(new_cb, data); }
}; // }}}

struct YieldAwaiter { // {{{
	// This class lets "yield" (really co_await YieldAwaiter()) send an argument and return a value.
	coroutine::promise_type *promise;
	std::shared_ptr <WebObject> from_coroutine;	// cache for passed value, because promise is not accessible when it is passed.
	YieldAwaiter(std::shared_ptr <WebObject> from_coroutine) : from_coroutine(from_coroutine) { STARTFUNC; }
	std::shared_ptr <WebObject> await_resume();
	bool await_ready() { STARTFUNC; return false; }
	bool await_suspend(coroutine::handle_type handle);
	~YieldAwaiter() { STARTFUNC; }
}; // }}}

struct GetHandleAwaiter { // {{{
	// This class lets a coroutine retrieve its wakeup handle.
	coroutine::handle_type handle;
	bool await_ready() { STARTFUNC; return false; }
	bool await_suspend(coroutine::handle_type the_handle) { STARTFUNC; handle = the_handle; return false; }
	coroutine::handle_type await_resume() { STARTFUNC; return handle; }
}; // }}}
#define GetHandle() (co_await GetHandleAwaiter())

// Defines to make yield slightly less horrible. {{{
#define Yield(from_coroutine) (co_await YieldAwaiter(from_coroutine))

// Similar to Python's "yield from". Usage:
// YieldFrom(auto, variable_name, coroutine, first_argument);
// std::shared_ptr <WebObject> variable_name; YieldFrom(, variable_name, coroutine, first_argument);
#define YieldFromFull(vardef, var, target, firstarg) \
	vardef var = target(firstarg); \
	while (!bool(target)) { \
		std::shared_ptr <WebObject> next = Yield(var); \
		var = target(next); \
	}
// Syntactic sugar for common case.
#define YieldFrom(var, target) YieldFromFull(auto, var, target, WebNone::create())
// }}}

// vim: set foldmethod=marker :

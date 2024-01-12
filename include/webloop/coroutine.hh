#ifndef _COROUTINE_HH
#define _COROUTINE_HH

/*
Functions called, in order.

For awaiters:
- await_ready() -> If it returns true, coroutine is not suspended; if false, it is.
- await_suspend(self_handle) -> return true (or void) to suspend; false to keep running; a coroutine handle to schedule another (tail recursion allowed).
- await_resume() -> always called; what it returns is passed to the (now resuming) coroutine.
*/

// Includes. {{{
#include "webobject.hh"
#include <coroutine>
#include <string>
#include <list>
#include <iostream>
#include <vector>
#include <map>
#include <cassert>
#include <endian.h>
#include <ieee754.h>
#include <sstream>
#include <memory>
// }}}

namespace Webloop {

struct FinalSuspendAwaitable;

struct coroutine { // {{{
	struct promise_type;
	struct CbBase {};	// Not actually the base, the class just uses it as such.
	using handle_type = std::coroutine_handle <promise_type>;
	struct promise_type { // {{{
		// from_coroutine = handle(to_coroutine); to_coroutine = Yield(from_coroutine);
		std::shared_ptr <WebObject> from_coroutine;
		std::shared_ptr <WebObject> to_coroutine;

		// Callback is used for launched coroutines. It should not be set for YieldFrom.
		typedef void (CbBase::*Cb)(std::shared_ptr <WebObject> ret);
		CbBase *cb_base;
		Cb cb;
		handle_type continuation;	// Target coroutine to run when we are finished. Used by YieldFrom.

		// Constructor and related functions.
		promise_type() : from_coroutine(), to_coroutine(), cb_base(nullptr), cb(), continuation() { STARTFUNC; }
		~promise_type() { STARTFUNC; }
		coroutine get_return_object() { STARTFUNC; return coroutine(handle_type::from_promise(*this)); }
		std::suspend_always initial_suspend() noexcept { STARTFUNC; return {}; }
		inline FinalSuspendAwaitable final_suspend() noexcept;
		void unhandled_exception() { STARTFUNC; }

		// If co_yield is used, this function is called. If Yield is used, the YieldAwaiter class below is used.
		std::suspend_always yield_value(std::shared_ptr <WebObject> v) { STARTFUNC; from_coroutine.swap(v); return {}; }

		// co_return will call a callback function, if it is registered.
		std::suspend_never return_value(std::shared_ptr <WebObject> v) {
			STARTFUNC;
			from_coroutine.swap(v);
			if (cb)
				(cb_base->*cb)(from_coroutine);
			return {};
		}
		// Set callback. Not a coroutine; used by user code.
		void set_cb(CbBase *base, Cb new_cb) { STARTFUNC; cb_base = base; cb = new_cb; }
		// Set a coroutine continuation; used by YieldFrom.
		void set_continuation(handle_type target) { STARTFUNC; continuation = target; }
	}; // }}}
	handle_type handle;
	coroutine(handle_type handle) : handle(handle) { STARTFUNC; }
	~coroutine() { STARTFUNC; }
	static std::shared_ptr <WebObject> activate(handle_type *handle, std::shared_ptr <WebObject> to_coroutine = WebNone::create());	// Argument is returned from Yield.
	std::shared_ptr <WebObject> operator()(std::shared_ptr <WebObject> to_coroutine = WebNone::create()) { return activate(&handle, to_coroutine); }
	operator bool() { STARTFUNC; return handle.done(); }
	template <class Base> void set_cb(Base *base, void (Base::*new_cb)(std::shared_ptr <WebObject> ret)) { STARTFUNC; handle.promise().set_cb(reinterpret_cast <CbBase *>(base), reinterpret_cast <promise_type::Cb>(new_cb)); }
	void set_continuation(coroutine::handle_type target) { STARTFUNC; handle.promise().set_continuation(target); }
	std::shared_ptr <WebObject> result() { return handle.promise().from_coroutine; }
}; // }}}

struct FinalSuspendAwaitable { // {{{
	// Handle continuation if present, otherwise return to caller.
	bool await_ready() noexcept { STARTFUNC; return false; }
	std::coroutine_handle <> await_suspend(coroutine::handle_type handle) noexcept;
	void await_resume() noexcept {} // This is only used as final_suspend, so it never resumes.
}; // }}}
FinalSuspendAwaitable coroutine::promise_type::final_suspend() noexcept { return {}; }

struct YieldAwaiter { // {{{
	// This class lets "yield" (really co_await YieldAwaiter()) send an argument and return a value.
	coroutine::handle_type handle;
	std::shared_ptr <WebObject> from_coroutine;	// cache for passed value, because handle is not accessible when it is passed.
	YieldAwaiter(std::shared_ptr <WebObject> from_coroutine) : from_coroutine(from_coroutine) { STARTFUNC; }
	bool await_ready() noexcept { STARTFUNC; return false; }
	bool await_suspend(coroutine::handle_type handle) noexcept;
	std::shared_ptr <WebObject> await_resume() noexcept;
	~YieldAwaiter() { STARTFUNC; }
}; // }}}

struct YieldFromAwaiter { // {{{
	// This class lets YieldFrom prepare another coroutine for yielding from it.
	coroutine::handle_type my_handle;
	coroutine::handle_type target_handle;
	YieldFromAwaiter(coroutine target) : target_handle(target.handle) { STARTFUNC; }
	bool await_ready() noexcept { STARTFUNC; return false; }
	coroutine::handle_type await_suspend(coroutine::handle_type handle) noexcept;
	std::shared_ptr <WebObject> await_resume() noexcept;
	~YieldFromAwaiter() { STARTFUNC; }
}; // }}}

struct GetHandleAwaiter { // {{{
	// This class lets a coroutine retrieve its wakeup handle.
	coroutine::handle_type handle;
	bool await_ready() noexcept { STARTFUNC; return false; }
	bool await_suspend(coroutine::handle_type the_handle) noexcept { STARTFUNC; handle = the_handle; return false; }
	coroutine::handle_type await_resume() noexcept { STARTFUNC; return handle; }
}; // }}}
#define GetHandle() (co_await Webloop::GetHandleAwaiter())

// Defines to make yield slightly less horrible. {{{
#define Yield(from_coroutine) (co_await Webloop::YieldAwaiter(from_coroutine))
#define YieldFrom(target) (co_await Webloop::YieldFromAwaiter(target))
// }}}

}

#endif

// vim: set foldmethod=marker :

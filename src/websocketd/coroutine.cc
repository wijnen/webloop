#include "coroutine.hh"

std::shared_ptr <WebNone> coroutine::RETURNED = WebNone::create();

std::shared_ptr <WebObject> coroutine::operator()(std::shared_ptr <WebObject> to_coroutine) { // {{{
	STARTFUNC;
	handle.promise().to_coroutine = to_coroutine;
	handle();
	std::shared_ptr <WebObject> ret;
	handle.promise().from_coroutine.swap(ret);
	return std::move(ret);
} // }}}

std::shared_ptr <WebObject> YieldAwaiter::await_resume() { // {{{
	STARTFUNC;
	std::shared_ptr <WebObject> ret;
	promise->to_coroutine.swap(ret);
	return std::move(ret);
} // }}}

bool YieldAwaiter::await_suspend(coroutine::handle_type handle) { // {{{
	// Either this or promise->yield_value() is called; the effect is the same: promise->from_coroutine is filled.
	STARTFUNC;
	promise = &handle.promise();
	promise->from_coroutine.swap(from_coroutine);
	from_coroutine.reset();
	return true;
} // }}}

// vim: set foldmethod=marker :

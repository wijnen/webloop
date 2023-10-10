#include "coroutine.hh"

std::shared_ptr <WebObject> coroutine::activate(coroutine::handle_type *handle, std::shared_ptr <WebObject> to_coroutine) { // {{{
	STARTFUNC;
	if (handle->done())
		return handle->promise().from_coroutine;
	//if (to_coroutine)
	//	std::cerr << "to coroutine: " << to_coroutine->print() << std::endl;
	//else
	//	std::cerr << "no to coroutine" << std::endl;
	handle->promise().to_coroutine = to_coroutine;
	(*handle)();
	if (handle->done())
		return handle->promise().from_coroutine;
	std::shared_ptr <WebObject> ret;
	handle->promise().from_coroutine.swap(ret);
	return ret;
} // }}}

std::shared_ptr <WebObject> YieldAwaiter::await_resume() { // {{{
	STARTFUNC;
	std::shared_ptr <WebObject> ret;
	promise->to_coroutine.swap(ret);
	//if (ret)
	//	std::cerr << "resuming with result: " << ret->print() << std::endl;
	//else
	//	std::cerr << "resuming with no result." << std::endl;
	return ret;
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

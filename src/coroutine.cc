#include "webloop/coroutine.hh"

namespace Webloop {

// Final suspend awaiter for all coroutines: make continuation run, if it exists.
std::coroutine_handle <> FinalSuspendAwaitable::await_suspend(coroutine::handle_type handle) noexcept { // {{{
	STARTFUNC;
	auto *promise = &handle.promise();

	// Check if a continuation is present.
	if (promise->continuation == coroutine::handle_type())
		return std::noop_coroutine();

	// Store return value into target.
	auto *target = &promise->continuation.promise();
	target->to_coroutine.swap(promise->from_coroutine);

	// Return into new coroutine.
	return promise->continuation;
} // }}}

// Activate (wake up) a coroutine.
std::shared_ptr <WebObject> coroutine::activate(coroutine::handle_type *handle, std::shared_ptr <WebObject> to_coroutine) { // {{{
	STARTFUNC;
	// If coroutine is done, return the return value immediately.
	if (handle->done())
		return handle->promise().from_coroutine;

	// Set argument and resume the coroutine.
	handle->promise().to_coroutine = to_coroutine;
	(*handle)();

	// If coroutine is now done, return the returned value, and keep it in place.
	if (handle->done())
		return handle->promise().from_coroutine;

	// Otherwise take over the returned value.
	std::shared_ptr <WebObject> ret;
	handle->promise().from_coroutine.swap(ret);
	return ret;
} // }}}

// Suspend a coroutine using Yield.
bool YieldAwaiter::await_suspend(coroutine::handle_type handle) noexcept { // {{{
	// Either Yield or handle.promise().yield_value() is called; the effect is the same: handle.promise().from_coroutine is filled.
	STARTFUNC;
	this->handle = handle;
	handle.promise().from_coroutine.swap(from_coroutine);
	from_coroutine.reset();
	return true;
} // }}}

// Resume a coroutine that was suspended using Yield.
std::shared_ptr <WebObject> YieldAwaiter::await_resume() noexcept { // {{{
	STARTFUNC;
	std::shared_ptr <WebObject> ret;
	handle.promise().to_coroutine.swap(ret);
	return ret;
} // }}}

// Suspend a coroutine using YieldFrom.
coroutine::handle_type YieldFromAwaiter::await_suspend(coroutine::handle_type handle) noexcept { // {{{
	STARTFUNC;
	my_handle = handle;
	target_handle.promise().set_continuation(handle);
	return target_handle;
} // }}}

// Resume a coroutine that was suspended using YieldFrom.
std::shared_ptr <WebObject> YieldFromAwaiter::await_resume() noexcept { // {{{
	STARTFUNC;
	std::shared_ptr <WebObject> ret;
	my_handle.promise().to_coroutine.swap(ret);
	return ret;
} // }}}

}

// vim: set foldmethod=marker :

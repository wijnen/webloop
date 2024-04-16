#include "webloop/coroutine.hh"

namespace Webloop {

std::shared_ptr <WebObject> *debug_ptr;
int coroutine::next_name_id;

// Final suspend awaiter for all coroutines: make continuation run, if it exists.
std::coroutine_handle <> FinalSuspendAwaitable::await_suspend(coroutine::handle_type handle) noexcept { // {{{
	STARTFUNC;
	auto *promise = &handle.promise();

	// Check if a continuation is present.
	if (promise->continuation == coroutine::handle_type()) {
		// No continuation, then result pointers are used (if set).
		if (promise->is_done != nullptr)
			*promise->is_done = true;
		if (promise->retval != nullptr)
			promise->retval->swap(promise->from_coroutine);
		handle.destroy();
		return std::noop_coroutine();
	}
	// Continuation: ignore result pointers.

	auto continuation = promise->continuation;
	// Store return value into target.
	auto *target = &continuation.promise();
	target->to_coroutine.swap(promise->from_coroutine);

	// Return into new coroutine.
	handle.destroy();
	return continuation;
} // }}}

// Activate (wake up) a coroutine.
std::shared_ptr <WebObject> coroutine::activate(coroutine::handle_type *handle, std::shared_ptr <WebObject> to_coroutine, bool *is_done) { // {{{
	STARTFUNC;
	// Set return value pointer, so return value can be read after coroutine is destroyed.
	std::shared_ptr <WebObject> retval;
	bool done = false;
	if (is_done == nullptr)
		is_done = &done;
	handle->promise().retval = &retval;
	handle->promise().is_done = is_done;
	// Set argument and resume the coroutine.
	handle->promise().to_coroutine = to_coroutine;
	try {
		(*handle)();
	}
	catch (char const *msg) {
		WL_log(std::string("Exception from coroutine: ") + msg);
		throw;
	}
	catch (std::string msg) {
		WL_log(std::string("Exception from coroutine: ") + msg);
		throw;
	}
	catch (...) {
		WL_log("Other exception from coroutine");
		throw;
	}

	// If coroutine is now done, the handle is no longer valid. The returned value was filled by it.
	// If it isn't done, retval was not touched, so it need to be filled.
	if (!*is_done)
		handle->promise().from_coroutine.swap(retval);

	return retval;
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

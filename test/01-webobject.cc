#define WEBLOOP_HELP "Test program for WebObjects."
#define WEBLOOP_CONTACT "Bas Wijnen <wijnen@debian.org>"
#define WEBLOOP_PACKAGE_NAME "webloop-test"

#include <webloop.hh>
#include <iostream>

/*
TEST OUTPUT: Calling function.
TEST OUTPUT: Function called with args [ 12, "Hoi", ] and kwargs { Foo: "Bar", }
TEST OUTPUT: returned "Function result"
TEST OUTPUT: Creating coroutine.
TEST OUTPUT: Launching coroutine.
TEST OUTPUT: coro called with args [ 1, 2, 3, ] and kwargs { }
TEST OUTPUT: yielded 56
TEST OUTPUT: Received None
TEST OUTPUT: returned 3.5
TEST OUTPUT: Member function called with args [ 2, 9, ] and kwargs { }
TEST OUTPUT: returned "Member result"
TEST OUTPUT: Member coro called with args [ "coro", "member", ] and kwargs { }
TEST OUTPUT: yielded 56
TEST OUTPUT: Received None
TEST OUTPUT: returned 3.5
*/

using namespace Webloop;

static std::shared_ptr <WebObject> function(std::shared_ptr <WebObject> args, std::shared_ptr <WebObject> kwargs) {
	std::cout << "Function called with args " << args->print() << " and kwargs " << kwargs->print() << std::endl;
	return "Function result"_ws;
}

static coroutine coro(std::shared_ptr <WebObject> args, std::shared_ptr <WebObject> kwargs) {
	std::cout << "coro called with args " << args->print() << " and kwargs " << kwargs->print() << std::endl;
	std::cout << "Received " << Yield(56_wi)->print() << std::endl;
	co_return 3.5_wf;
}

class testclass {
public:
	std::shared_ptr <WebObject> member(std::shared_ptr <WebObject> args, std::shared_ptr <WebObject> kwargs) {
		std::cout << "Member function called with args " << args->print() << " and kwargs " << kwargs->print() << std::endl;
		return "Member result"_ws;
	}

	coroutine coro_member(std::shared_ptr <WebObject> args, std::shared_ptr <WebObject> kwargs) {
		std::cout << "Member coro called with args " << args->print() << " and kwargs " << kwargs->print() << std::endl;
		std::cout << "Received " << Yield(56_wi)->print() << std::endl;
		co_return 3.5_wf;
	}
};

void run_coroutine(coroutine c) {
	std::shared_ptr <WebObject> r;
	bool done = false;
	while (true) {
		r = c(WN(), &done);
		if (done)
			break;
		if (&*r == nullptr)
			std::cout << "invalid value yielded!" << std::endl;
		else
			std::cout << "yielded " << r->print() << std::endl;
	}
	if (&*r == nullptr)
		std::cout << "invalid value returned!" << std::endl;
	else
		std::cout << "returned " << r->print() << std::endl;
}

void runner() {
	// TODO: Add tests for operators.

	auto f = WebFunctionPointer::create(&function);
	std::cout << "Calling function." << std::endl;
	run_coroutine((*f)(WV(12, "Hoi"), WM(WT("Foo", "Bar"))));

	auto ptr = WebCoroutinePointer::create(&coro);
	std::cout << "Creating coroutine." << std::endl;
	auto c = (*ptr)(WV(1, 2, 3), WM());
	std::cout << "Launching coroutine." << std::endl;
	run_coroutine(c);

	testclass tc;

	f = WebMemberPointer <testclass>::create(&tc, &testclass::member);
	c = (*f)(WV(2, 9));
	run_coroutine(c);

	f = WebCoroutineMemberPointer <testclass>::create(&tc, &testclass::coro_member);
	c = (*f)(WV("coro", "member"));
	run_coroutine(c);
}

int main(int argc, char **argv) {
	fhs_init(argc, argv);
	try {
		runner();
	}
	catch (char const *msg) {
		std::cerr << "Exception: " << msg << std::endl;
	}
	catch (std::string msg) {
		std::cerr << "Exception: " << msg << std::endl;
	}
	return 0;
}

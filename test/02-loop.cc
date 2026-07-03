#include <webloop/loop.hh>
#include <iostream>

/*
OUTPUT:
Timed handler called.
END OF OUTPUT.
*/

struct Timed {
	bool test() { std::cout << "Timed handler called.\n"; return false; }
	bool finish() { Webloop::Loop::get()->stop(); return false; }
};

int main(int /*argc*/, char ** /*argv*/)
{
	auto *loop = Webloop::Loop::get();
	Timed target;
	auto now = loop->now();
	Webloop::Loop::TimeoutRecord to(now + std::chrono::seconds(1),
			Webloop::Loop::Duration(0), &target, &Timed::test);
	Webloop::Loop::TimeoutRecord finish(now + std::chrono::seconds(2),
			Webloop::Loop::Duration(0), &target, &Timed::finish);
	loop->add_timeout(to);
	loop->add_timeout(finish);
	loop->run();
	return 0;
}

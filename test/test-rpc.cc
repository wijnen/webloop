#include "websocketd.hh"

struct Dummy {};

int main() {
	auto p = Webloop::RPC <Dummy>("8000");
	auto coro = p.fgcall("get_version", {}, {});
	coro();
	while (!bool(coro))
		Webloop::Loop::get()->iteration(true);
	auto version = coro();
	if (version)
		std::cout << version->print() << std::endl;
	else
		std::cout << "no version" << std::endl;
	return 0;
}

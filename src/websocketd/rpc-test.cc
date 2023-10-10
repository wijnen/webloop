#include "websocketd.hh"

int main() {
	auto p = RPC("8000");
	auto coro = p.fgcall("get_version", {}, {});
	coro();
	while (!bool(coro))
		Loop::get()->iteration(true);
	auto version = coro();
	if (version)
		std::cout << version->print() << std::endl;
	else
		std::cout << "no version" << std::endl;
	return 0;
}

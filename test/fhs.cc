#include "webloop.hh"

/*
TEST RESULT: 0
*/

using namespace Webloop;

int main(int argc, char **argv) {
	(void)&argc;
	init(argv, "fhs test", "0.1", "Bas Wijnen <wijnen@debian.org>");
	auto dirs = read_data_names("html", {}, true, true);
	for (auto d: dirs) {
		std::cerr << "dir: " << d << std::endl;
	}
	// TODO: Add tests here.
	return 0;
}

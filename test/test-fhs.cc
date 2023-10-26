#include "fhs.hh"

int main(int argc, char **argv) {
	(void)&argc;
	Webloop::Fhs fhs(argv, "fhs test", "0.1", "Bas Wijnen <wijnen@debian.org>");
	// TODO: Add tests here.
	return 0;
}

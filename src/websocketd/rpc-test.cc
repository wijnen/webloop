#include "websocketd.hh"

int main() {
	p = RPC("7000");
	std::cout << p.fgcall("get_version", {}, {}) << std::endl;
	return 0;
}

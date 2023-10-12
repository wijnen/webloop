#include "websocketd.hh"

class Connection {
public:
	Connection(Httpd <Connection>::Connection &base) { (void)&base; }
};

int main() {
	Httpd <Connection> httpd("5376", {}, {}, nullptr, nullptr);
	Loop::get()->run();
	return 0;
}

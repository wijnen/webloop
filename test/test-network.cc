#include "network.hh"
#include <iostream>

/*
bool cb(void *data) {
	std::cerr << "ping" << std::endl;
	reinterpret_cast <Loop *>(data)->stop();
	return true;
}

bool icb(void *data) {
	std::cerr << "#";
	return true;
}
*/

class Remote {
	Webloop::Socket <Remote> remote, local;
public:
	Remote(std::string const &raddr, int laddr) :
		remote(raddr, this),
		local(laddr, this)
	{
		remote.set_disconnect_cb(&Remote::disconnect);
		remote.read(&Remote::readcb);
		local.read_lines(&Remote::stdin_cb);
	}
	void readcb(std::string &buffer) {
		std::cerr << "\"" << buffer << "\"" << std::endl;
		if (buffer == "exit\n")
			Webloop::Loop::get()->stop();
		buffer.clear();
	}
	void disconnect() {
		WL_log("disconnect");
		Webloop::Loop::get()->stop();
	}
	void stdin_cb(std::string const &line) {
		remote.send(line + "\n");
	}
};

int main() {
	Remote sockets("http://localhost:8567/foo?bar#baz", STDIN_FILENO);
	try {
		Webloop::Loop::get()->run();
	}
	catch (char const *msg) {
		std::cerr << "error: " << msg << std::endl;
		throw;
	}
	return 0;
}

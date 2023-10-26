#include "network.hh"
#include "webobject.hh"
#include <iostream>

class S {
	std::list <Webloop::Socket <S> > sockets;
	Webloop::Socket <S> local;
	Webloop::Server <S, S> server;
public:
	S() : sockets(), local(STDIN_FILENO, this), server("8567", this, &S::acceptor) {
		local.read_lines(&S::stdin_cb);
	}

	void readcb(std::string &buffer) {
		std::cerr << Webloop::WebString(buffer).dump() << std::endl;
		if (buffer == "exit\n")
			Webloop::Loop::get()->stop();
		buffer.clear();
	}

	void acceptor(Webloop::Socket <S> *remote) {
		for (auto &i: sockets)
			i.send("New connection.\n");
		remote->read(&S::readcb);
		sockets.emplace_back(std::move(*remote));
	}

	void stdin_cb(std::string const &line) {
		for (auto &i: sockets)
			i.send(line + "\n");
	}
};

int main() {
	S s;
	std::cerr << "running" << std::endl;
	try {
		Webloop::Loop::get()->run();
	}
	catch (char const *msg) {
		std::cerr << "error: " << msg << std::endl;
		throw;
	}
	return 0;
}

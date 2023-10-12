#include "network.hh"
#include <iostream>

struct Vars {
	Loop loop;
	Server server;
	Socket local;
	Socket remote;
};

void readcb(std::string &buffer, void *user_data) {
	Vars *vars = reinterpret_cast <Vars *>(user_data);
	std::cerr << "\"" << buffer << "\"" << std::endl;
	if (buffer == "exit\n")
		vars->loop.stop();
	buffer.clear();
}

void stdin_cb(std::string &&line, void *user_data) {
	Vars *vars = reinterpret_cast <Vars *>(user_data);
	for (auto i: vars->server.remotes)
		i->send("'" + line + "'\n");
}

void acceptor(Socket &&remote, void *user_data) {
	Vars *vars = reinterpret_cast <Vars *>(user_data);
	for (auto i: vars->server.remotes)
		i->send("New connection.\n");
	vars->remote = std::move(remote);
	remote.read(readcb);
}

int main() {
	//Loop loop;
	//loop.add_timeout({loop.now() + 1s, 100ms, cb, &loop});
	//loop.add_idle({icb, nullptr});
	
	Vars vars { {}, {"8567", acceptor, nullptr, &vars}, {STDIN_FILENO, &vars}, {} };
	vars.local.read_lines(stdin_cb);
	std::cerr << "running" << std::endl;
	try {
		vars.loop.run();
	}
	catch (char const *msg) {
		std::cerr << "error: " << msg << std::endl;
		throw;
	}
	return 0;
}

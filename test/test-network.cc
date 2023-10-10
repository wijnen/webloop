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

struct Vars {
	Loop loop;
	Socket remote;
	Socket local;
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
	vars->remote.send("'" + line + "'\n");
}

int main() {
	//Loop loop;
	//loop.add_timeout({loop.now() + 1s, 100ms, cb, &loop});
	//loop.add_idle({icb, nullptr});
	
	Vars vars { {}, {"http://localhost:8567/foo?bar#baz", &vars}, {STDIN_FILENO, &vars} };
	vars.remote.read(readcb);
	vars.local.read_lines(stdin_cb);
	try {
		vars.loop.run();
	}
	catch (char const *msg) {
		std::cerr << "error: " << msg << std::endl;
		throw;
	}
	return 0;
}

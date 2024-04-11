#define WEBLOOP_HELP "Httpd test program"
#define WEBLOOP_CONTACT "Bas Wijnen <wijnen@debian.org>"
#define WEBLOOP_PACKAGE_NAME "webloop-test"

#include <algorithm>
#include "webloop/websocketd.hh"

using namespace Webloop;
using namespace std::literals;

class Client {
};

class Owner {
	Websocket <Owner> websocket;
public:
	void accept(Httpd <Owner>::Connection &c) {
		websocket = Websocket <Owner> (std::move(c.socket), this, &Owner::receiver, {c.httpd->get_loop(), c.httpd->get_keepalive()});
		WL_log("new websocket");
	}
	void receiver(std::string const &data) {
		WL_log("received:" + WebString(data).dump());
		Loop::get()->stop();
	}

	bool later() {
		// Set up client.
		Websocket <Client> client("5376");
		client.send("Dit is data");
		return false;
	}
};

int main(int argc, char **argv) {
	fhs_init(argc, argv);

	// Set up server.
	Owner owner;
	Httpd <Owner> httpd(&owner, "5376");
	httpd.set_accept(&Owner::accept);

	auto loop = Loop::get();
	loop->add_timeout({ loop->now() + 100ms, {}, &owner, &Owner::later });

	Loop::get()->run();
	return 0;
}

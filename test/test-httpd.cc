#include "websocketd.hh"

class S {
};

class T {
	Webloop::Websocket <T> websocket;
public:
	T(Webloop::Httpd <T, S>::NewSocket &&s, Webloop::Httpd <T, S> *server) : websocket(std::move(s), this, &T::receiver) {
		(void)&server;
		WL_log("new websocket");
	}
	void receiver(std::string const &data) {
		WL_log("received:" + Webloop::WebString(data).dump());
	}
};

int main() {
	S s;
	Webloop::Httpd <T, S> httpd(&s, "5376");
	Webloop::Loop::get()->run();
	return 0;
}

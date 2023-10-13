#include "websocketd.hh"

class T {
	Websocket <T> websocket;
public:
	T(Socket <T> &&s) : websocket(std::move(s), &T::receiver) {
		log("new websocket");
	}
	void receiver(std::string const &data) {
		log("received:" + WebString(data).dump());
	}
};


int main() {
	Httpd <T> httpd("5376");
	Loop::get()->run();
	return 0;
}

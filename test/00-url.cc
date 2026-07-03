#include <webloop/url.hh>
#include <iostream>

/*
OUTPUT:
Source: 'https://www.wijnen.me:666/test?attr=3&p=x#label'.
Scheme: 'https://'.
Host: 'www.wijnen.me'.
Port: ':666'.
Path: '/test'.
Parameters: ''.
Raw Query: '?attr=3&p=x'.
Fragment: '#label'.
Service: '666'.
Unix: ''.
Built host: 'www.wijnen.me:666'.
Built request: '/test?attr=3&p=x'.
Query key 'attr' = '3'.
Query key 'p' = 'x'.
Source: '/run/some-socket.pipe'.
Scheme: ''.
Host: ''.
Port: ''.
Path: '/run/some-socket.pipe'.
Parameters: ''.
Raw Query: ''.
Fragment: ''.
Service: ''.
Unix: '/run/some-socket.pipe'.
Built host: ''.
Built request: '/run/some-socket.pipe'.
END OF OUTPUT.
*/

void check(std::string const &url)
{
	Webloop::URL tst(url);
	std::cout << std::format("Source: '{}'.\n", tst.src);
	std::cout << std::format("Scheme: '{}'.\n", tst.scheme);
	std::cout << std::format("Host: '{}'.\n", tst.host);
	std::cout << std::format("Port: '{}'.\n", tst.port);
	std::cout << std::format("Path: '{}'.\n", tst.path);
	std::cout << std::format("Parameters: '{}'.\n", tst.parameters);
	std::cout << std::format("Raw Query: '{}'.\n", tst.rawquery);
	std::cout << std::format("Fragment: '{}'.\n", tst.fragment);
	std::cout << std::format("Service: '{}'.\n", tst.service);
	std::cout << std::format("Unix: '{}'.\n", tst.unix);
	std::cout << std::format("Built host: '{}'.\n", tst.build_host());
	std::cout << std::format("Built request: '{}'.\n", tst.build_request());
	for (auto kv: tst.query) {
		std::cout << std::format("Query key '{}' = '{}'.\n",
				kv.first, kv.second);
	}
}

int main(int /*argc*/, char ** /*argv*/)
{
	check("https://www.wijnen.me:666/test?attr=3&p=x#label");
	check("/run/some-socket.pipe");
	return 0;
}

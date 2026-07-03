#ifndef _URL_HH
#define _URL_HH

#include <string>
#include <map>
#include <list>

namespace Webloop {

	// TODO: Turn into std::string_view.
struct URL {
	// <scheme>:// <host> :<port> /<path> ;<parameters> ?<query> #<fragment>
	std::string src;
	std::string scheme, host, port, path, parameters, rawquery, fragment;
	std::string service;	// scheme if present, otherwise port.
	std::string unix;	// unix domain socket path, or empty.

	// Store first value for multiple.
	// No values are stored as empty strings.
	std::map <std::string, std::string> query;

	// All query keys.
	std::map <std::string, std::list <std::string> > multiquery;

	URL() {}
	URL(std::string const &url);
	std::string build_host() { return host + port; }
	std::string build_request();

	// Copy first pos characters, encode the rest.
	static std::string encode(std::string const &src,
			std::string::size_type pos = 0);

	static std::string decode(std::string_view const &src);
	void clear();
	std::string print() const;
};

}

#endif

// vim: set foldmethod=marker :

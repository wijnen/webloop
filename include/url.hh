#ifndef _URL_HH
#define _URL_HH

#include <string>
#include <map>
#include <list>

struct URL {
	// <scheme>:// <host> :<port> /<path> ;<parameters> ?<query> #<fragment>
	std::string src;
	std::string scheme, host, port, path, parameters, rawquery, fragment;
	std::string service;	// scheme if present, otherwise port.
	std::string unix;	// unix domain socket path, or empty.
	std::map <std::string, std::string> query;	// Store first value for multiple. No values are stored as empty strings.
	std::map <std::string, std::list <std::string> > multiquery;	// All query keys.
	URL() {}
	URL(std::string const &url);
	std::string build_host() { return host + port; }
	std::string build_request();
};

#endif

// vim: set foldmethod=marker :

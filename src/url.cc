#include "url.hh"
#include "tools.hh"

URL::URL(std::string const &url) : src(url) {
	// Find scheme. {{{
	auto q = url.find(':');
	auto p = url.find("://");
	if (p != std::string::npos && q == p) {
		service = url.substr(0, p);
		p += 3;
		scheme = url.substr(0, p);
	}
	else
		p = 0;
	// }}}
	// Find host. {{{
	q = url.find_first_of(":/;?#", p);
	if (q == std::string::npos) {
		host = url.substr(p);
		return;
	}
	host = url.substr(p, q - p);
	p = q;
	// }}}
	if (url[p] == ':') { // port. {{{
		q = url.find_first_of("/;?#", p + 1);
		if (q == std::string::npos) {
			port = url.substr(p);
			if (service.empty())
				service = url.substr(p + 1);
			return;
		}
		port = url.substr(p, q - p);
		if (service.empty())
			service = port.substr(1);
		p = q;
	} // }}}
	if (url[p] == '/') { // path. {{{
		q = url.find_first_of(";?#", p);
		if (q == std::string::npos) {
			path = url.substr(p);
			// No parameters, no query, no fragment. If there is also no scheme and no port, fill unix with detected path.
			if (scheme.empty() && port.empty())
				unix = host + path;
			return;
		}
		path = url.substr(p, q - p);
		p = q;
	} // }}}
	// Store path for explicit unix domain sockets.
	if (scheme == "unix://")
		unix = host + path;	// TODO allow escaped characters.
	if (url[p] == ';') { // parameters. {{{
		q = url.find_first_of("?#", ++p);
		if (q == std::string::npos) {
			parameters = url.substr(p);
			return;
		}
		parameters = url.substr(p, q - p);
		p = q;
	} // }}}
	if (url[p] == '?') { // query. {{{
		q = url.find_first_of("#", ++p);
		if (q == std::string::npos)
			rawquery = url.substr(p);
		else
			rawquery = url.substr(p, q - p);
		p = q;
	} // }}}
	if (p != std::string::npos) { // fragment. {{{
		fragment = url.substr(p);
	} // }}}
	// Parse query string. {{{
	// Split into parts.
	auto m = split(rawquery, -1, 1, "&");
	// Store key-value pairs.
	for (auto kv: m) {
		std::string key, value;
		p = kv.find('=');
		if (p == std::string::npos)
			key = kv;
		else {
			key = kv.substr(0, p);
			value = kv.substr(p + 1);
		}
		// TODO: decode key and value.
		if (!multiquery.contains(key)) {
			multiquery[key] = std::list <std::string> ();
			query[key] = value;
		}
		multiquery[key].push_back(value);
	}
	// }}}
}

std::string URL::build_request() { // {{{
	std::string ret = path;
	if (ret.empty() || ret[0] != '/')
		ret = "/" + ret;
	return ret + parameters + rawquery + fragment;
} // }}}

// vim: set foldmethod=marker :

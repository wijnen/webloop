#include <sstream>
#include <iomanip>
#include "url.hh"
#include "tools.hh"

namespace Webloop {

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
			// Always override scheme if port is specified.
			service = url.substr(p + 1);
			return;
		}
		port = url.substr(p, q - p);
		// Always override scheme if port is specified.
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
		unix = decode(host + path);	// this allows, but does not require, for slashes to be encoded. The purpose is to be able to encode the other special characters.
	if (url[p] == ';') { // parameters. {{{
		q = url.find_first_of("?#", p);
		if (q == std::string::npos) {
			parameters = decode(url.substr(p));
			return;
		}
		parameters = decode(url.substr(p, q - p));
		p = q;
	} // }}}
	if (url[p] == '?') { // query. {{{
		q = url.find_first_of("#", p);
		if (q == std::string::npos)
			rawquery = url.substr(p);
		else
			rawquery = url.substr(p, q - p);
		p = q;
	} // }}}
	if (p != std::string::npos) { // fragment. {{{
		fragment = decode(url.substr(p));
	} // }}}
	// Parse query string. {{{
	// Split into parts.
	auto m = split(rawquery, -1, 1, "&");
	// Store key-value pairs.
	for (auto kv: m) {
		std::string key, value;
		p = kv.find('=');
		if (p == std::string::npos) {
			key = decode(kv);
			value.clear();
		}
		else {
			key = decode(kv.substr(0, p));
			value = decode(kv.substr(p + 1));
		}
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
	return ret + encode(parameters, 1) + rawquery; // + encode(fragment, 1); The fragment probably should not be sent to the server.
} // }}}

std::string URL::encode(std::string const &src, std::string::size_type pos) { // {{{
	// Replace URL parts by escape codes.
	if (src.size() <= pos)
		return src;
	std::string ret = src.substr(0, pos);
	for (auto i = pos; i < src.size(); ++i) {
		auto c = src[i];
		if (c <= 32 || c >= 127 || c == ':' || c == '/' || c == ';' || c == '?' || c == '#' || c == '&' || c == '%')
			ret += (std::ostringstream() << "%" << std::hex << std::setfill('0') << std::setw(2) << (c & 0xff)).str();
		else
			ret += c;
	}
	return ret;
} // }}}

std::string URL::decode(std::string const &src) { // {{{
	std::string ret;
	std::string::size_type pos = 0;
	while (pos < src.size()) {
		auto q = src.find('%', pos);
		if (q == std::string::npos) {
			ret += src.substr(pos);
			return ret;
		}
		if (q + 3 > src.size()) {
			WL_log("Warning: decode found incomplete escape");
		}
		ret += src.substr(pos, q - pos);
		ret += char(std::stoi(src.substr(q + 1, 2), nullptr, 16) & 0xff);
		pos = q + 3;
	}
	return ret;
} // }}}

std::string URL::print() const { // {{{
	std::ostringstream ret;
	ret << "URL(" << src << ") {\n";
	ret << "\tscheme: " << scheme << "\n";
	ret << "\thost: " << host << "\n";
	ret << "\tport: " << port << "\n";
	ret << "\tpath: " << path << "\n";
	ret << "\tparameters: " << parameters << "\n";
	ret << "\tquery (raw): " << rawquery << "\n";
	ret << "\tfragment: " << fragment << "\n";
	ret << "Computed:\n";
	ret << "\tservice: " << service << "\n";
	ret << "\tunix: " << unix << "\n";
	ret << "\tquery:\n";
	for (auto i: query)
		ret << "\t\t" << i.first << " = " << i.second << "\n";
	ret << "\tmulti query:\n";
	for (auto i: multiquery) {
		ret << "\t\t" << i.first << ":\n";
		for (auto j: i.second)
			ret << "\t\t\t" << j << "\n";
	}
	ret << "}\n";
	return ret.str();
} // }}}

void URL::clear() { // {{{
	src.clear();
	scheme.clear();
	host.clear();
	port.clear();
	path.clear();
	parameters.clear();
	rawquery.clear();
	fragment.clear();
	service.clear();
	unix.clear();
	query.clear();
	multiquery.clear();
} // }}}

}

// vim: set foldmethod=marker :

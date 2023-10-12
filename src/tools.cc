#include "tools.hh"
#include <sstream>
#include <ctime>
#include <iostream>

static int initial_debug() {
	auto debug = std::getenv("DEBUG");
	if  (!debug)
		return 0;
	std::istringstream s(debug);
	int ret;
	s >> ret;
	return ret;
}

// Debug level, set from DEBUG environment variable.
// * 0: No debugging (default).
// * 1: Tracebacks on errors.
// * 2: Incoming and outgoing RPC packets.
// * 3: Incomplete packet information.
// * 4: All incoming and outgoing data.
// * 5: Non-websocket data.
int DEBUG = initial_debug();

std::string strip(std::string const &src, std::string const &chars) { // {{{
	auto first = src.find_first_not_of(chars);
	if (first == std::string::npos)
		return std::string();
	auto last = src.find_last_not_of(chars);
	auto ret = src.substr(first, last - first + 1);
	if (DEBUG > 4)
		log("stripped: '" + ret + "'");
	return ret;
} // }}}

std::vector <std::string> split(std::string const &src, int maxcuts, std::string::size_type pos, std::string const &chars) { // {{{
	std::vector <std::string> ret;
	while (maxcuts < 0 || ret.size() < unsigned(maxcuts)) {
		pos = src.find_first_not_of(chars, pos);
		if (pos == std::string::npos)
			return ret;
		auto n = src.find_first_of(chars, pos);
		if (n == std::string::npos) {
			ret.push_back(src.substr(pos));
			return ret;
		}
		ret.push_back(src.substr(pos, n - pos));
		pos = n;
	}
	pos = src.find_first_not_of(chars, pos);
	ret.push_back(src.substr(pos));
	return ret;
} // }}}

bool startswith(std::string const &data, std::string const &needle, std::string::size_type p) { // {{{
	if (data.size() - p < needle.size())
		return false;
	return data.substr(p, needle.size()) == needle;
} // }}}

std::string upper(std::string const &src) { // {{{
	std::string ret;
	for (auto c: src)
		ret += std::toupper(c);
	return ret;
} // }}}

std::string lower(std::string const &src) { // {{{
	std::string ret;
	for (auto c: src)
		ret += std::tolower(c);
	return ret;
} // }}}

// Logging. {{{
std::ostream *log_output = &std::cerr;
bool log_date = false;

void set_log_output(std::ostream &target) { // {{{
	/* Change target for log().
	By default, log() sends its output to standard error.  This function is
	used to change the target.
	@param file: The new file to write log output to.
	@return None.
	*/
	log_output = &target;
	log_date = true;
} // }}}

void log_impl(std::string const &message, std::string const &filename, std::string const &funcname, int line) { // {{{
	/* Log a message.
	Write a message to log (default standard error, can be changed with
	set_log_output()).  A timestamp is added before the message and a
	newline is added to it.
	@param message: The message to log. Multiple arguments are logged on separate lines. Newlines in arguments cause the message to be split, so they should not contain a closing newline.
	@param filename: Override filename to report.
	@param line: Override line number to report.
	@param funcname: Override function name to report.
	@param depth: How deep to enter into the call stack for function info.
	@return None.
	*/
	std::string date;
	if (log_date) {
		char buffer[100];
		auto t = std::time(nullptr);
		strftime(buffer, sizeof(buffer), log_date ? "%F %T" : "%T", std::gmtime(&t));
		date = std::string(buffer) + ": ";
	}
	(*log_output) << date << filename << ":" << line << ":" << funcname << ": " << message << std::endl;
} // }}}
// }}}

// vim: set foldmethod=marker :

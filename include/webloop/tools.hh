#ifndef _TOOLS_HH
#define _TOOLS_HH

#include <string>
#include <vector>
#include <iostream>

namespace Webloop {

// Debug level, set from DEBUG environment variable.
// * 0: No debugging (default).
// * 1: Tracebacks on errors.
// * 2: Incoming and outgoing RPC packets.
// * 3: Incomplete packet information.
// * 4: All incoming and outgoing data.
// * 5: Non-websocket data.
extern int DEBUG;

#define STARTFUNC do { std::cout << __FILE__ << ":" << __LINE__ << ": " << __FUNCTION__ << " entered." << std::endl; } while(0)
//#define STARTFUNC do {} while(0)

std::string strip(std::string const &src, std::string const &chars = " \t\r\n\v\f");
std::string lstrip(std::string const &src, std::string const &chars = " \t\r\n\v\f");
std::string rstrip(std::string const &src, std::string const &chars = " \t\r\n\v\f");
std::vector <std::string> split(std::string const &src, int maxcuts = -1, std::string::size_type pos = 0, std::string const &chars = " \t\r\n\v\f");
bool startswith(std::string const &data, std::string const &needle, std::string::size_type p = 0);
std::string upper(std::string const &src);
std::string lower(std::string const &src);

// Logging. {{{
extern std::ostream *log_output;
extern bool log_date;

void set_log_output(std::ostream &target);
void log_impl(std::string const &message, std::string const &filename, std::string const &funcname, int line);
#define WL_log(msg) Webloop::log_impl(msg, __FILE__, __PRETTY_FUNCTION__, __LINE__)
// }}}

// Implementation of RFC4648 (base64) and sha1, for use within websocket communication.
// These are available with better performance from other libraries. However,
// the performance is not relevant here and it's not much code. It is prefered
// to avoid depending on other libraries when possible.
std::string b64encode(std::string const &src);
std::string b64decode(std::string const &src, std::string::size_type &pos, bool allow_whitespace = false);

std::string sha1(std::string const &src);

}

#endif

// vim: set foldmethod=marker :

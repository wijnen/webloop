#ifndef _TOOLS_HH
#define _TOOLS_HH

#include <string>
#include <vector>

// Debug level, set from DEBUG environment variable.
// * 0: No debugging (default).
// * 1: Tracebacks on errors.
// * 2: Incoming and outgoing RPC packets.
// * 3: Incomplete packet information.
// * 4: All incoming and outgoing data.
// * 5: Non-websocket data.
extern int DEBUG;

std::string strip(std::string const &src, std::string const &chars = " \t\r\n\v\f");
std::vector <std::string> split(std::string const &src, int maxcuts = -1, std::string::size_type pos = 0, std::string const &chars = " \t\r\n\v\f");
bool startswith(std::string const &data, std::string const &needle, std::string::size_type p = 0);
std::string upper(std::string const &src);
std::string lower(std::string const &src);

// Logging. {{{
extern std::ostream *log_output;
extern bool log_date;

void set_log_output(std::ostream &target);
void log_impl(std::string const &message, std::string const &filename, std::string const &funcname, int line);
#define log(msg) log_impl(msg, __FILE__, __PRETTY_FUNCTION__, __LINE__)
// }}}

#endif

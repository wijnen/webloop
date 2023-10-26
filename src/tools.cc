#include "tools.hh"
#include "webobject.hh"
#include <sstream>
#include <ctime>
#include <iostream>
#include <cassert>

namespace Webloop {

static int initial_debug() { // {{{
	auto debug = std::getenv("DEBUG");
	if  (!debug)
		return 0;
	std::istringstream s(debug);
	int ret;
	s >> ret;
	return ret;
} // }}}

// Debug level, set from DEBUG environment variable.
// * 0: No debugging (default).
// * 1: Tracebacks on errors.
// * 2: Incoming and outgoing RPC packets.
// * 3: Incomplete packet information.
// * 4: All incoming and outgoing data.
// * 5: Non-websocket data.
int DEBUG = initial_debug();

// String utilities. {{{
std::string strip(std::string const &src, std::string const &chars) { // {{{
	auto first = src.find_first_not_of(chars);
	if (first == std::string::npos)
		return std::string();
	auto last = src.find_last_not_of(chars);
	auto ret = src.substr(first, last - first + 1);
	if (DEBUG > 4)
		WL_log("stripped: '" + ret + "'");
	return ret;
} // }}}

std::string lstrip(std::string const &src, std::string const &chars) { // {{{
	auto first = src.find_first_not_of(chars);
	if (first == std::string::npos)
		return std::string();
	auto ret = src.substr(first);
	if (DEBUG > 4)
		WL_log("lstripped: '" + ret + "'");
	return ret;
} // }}}

std::string rstrip(std::string const &src, std::string const &chars) { // {{{
	auto last = src.find_last_not_of(chars);
	if (last == std::string::npos)
		return std::string();
	auto ret = src.substr(0, last + 1);
	if (DEBUG > 4)
		WL_log("stripped: '" + ret + "'");
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
// }}}

// Base64 encoding and decoding. {{{
// Encode single base64 index into base64 output character.
static char toalphabet(int index) { // {{{
	// Index must be valid.
	static char const alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	assert(index >= 0 && index <= 0x3f);
	return alphabet[index];
} // }}}

// Decode single base64 alphabet letter into base64 index.
static int fromalphabet(char letter) { // {{{
	// Returns index for valid regular letters, -1 for padding, -2 for invalid letters.
	if (letter >= 'A' && letter <= 'Z')
		return letter - 'A';
	if (letter >= 'a' && letter <= 'z')
		return letter - 'a' + 26;
	if (letter >= '0' && letter <= '9')
		return letter - '0' + 52;
	if (letter == '+')
		return 62;
	if (letter == '/')
		return 63;
	if (letter == '=')
		return -1;
	return -2;
} // }}}

// Encode 3 binary characters into 4 base64 characters.
static void encode(std::string const &src, std::string::size_type &pos, char ret[4]) { // {{{
	ret[0] = toalphabet((src[pos] >> 2) & 0x3f);
	ret[1] = toalphabet(((src[pos] << 4) & 0x30) | ((src[pos + 1] >> 4) & 0x0f));
	ret[2] = toalphabet(((src[pos + 1] << 2) & 0x3c) | ((src[pos + 2] >> 6) & 0x03));
	ret[3] = toalphabet(src[pos + 2] & 0x3f);
	pos += 3;
} // }}}

// State for decoding result.
enum State { // {{{
	GOOD,	// Decoding was normal.
	PAD1,	// Decoding worked, there was 1 padding letter.
	PAD2,	// Decoding worked, there were 2 padding letters.
	BAD	// Decoding error.
}; // }}}

// Decode 4 base64 characters into 3 binary characters.
static void decode(std::string const &src, std::string::size_type &pos, char ret[3], State &state) { // {{{
	// Decode one unit of 4 base64 letters into 3 bytes. Updates pos. Sets state.
	int indices[4];
	for (int i = 0; i < 4; ++i) {
		indices[i] = fromalphabet(src[pos + i]);
		if (indices[i] < -1) {
			state = BAD;
			return;
		}
	}
	if (indices[0] < 0 || indices[1] < 0 || (indices[2] < 0 && indices[3] >= 0)) {
		state = BAD;
		return;
	}
	if (indices[2] < 0) {
		state = PAD2;
		indices[2] = 0;
		indices[3] = 0;
	}
	else if (indices[3] < 0) {
		state = PAD1;
		indices[3] = 0;
	}
	else
		state = GOOD;
	pos += 4;
	// Characters have been converted to indices and state has been set. Now decode the indices.
	ret[0] = ((indices[0] << 2) & 0xfc) | ((indices[1] >> 4) & 0x03);
	ret[1] = ((indices[1] << 4) & 0xf0) | ((indices[2] >> 2) & 0x0f);
	ret[2] = ((indices[2] << 6) & 0xc0) | (indices[3] & 0x3f);
} // }}}

// Encode a binary string into base64 encoding.
std::string b64encode(std::string const &src) { // {{{
	std::string::size_type pos = 0;
	std::string ret;
	char buffer[4];
	while (pos + 3 <= src.size()) {
		encode(src, pos, buffer);
		ret += std::string(buffer, 4);
	}
	int padlen = src.size() - pos;
	if (padlen > 0) {
		std::string f = src.substr(pos) + std::string("\0", 2);
		pos = 0;
		encode(f, pos, buffer);
		buffer[3] = '=';
		if (padlen == 1)
			buffer[2] = '=';
		ret += std::string(buffer, 4);
		pos = src.size();
	}
	return ret;
} // }}}

// Decode a base64 string into binary.
std::string b64decode(std::string const &src, std::string::size_type &pos, bool allow_whitespace) { // {{{
	// Return empty string and set pos to std::string::npos when any error is encountered.
	// With no error, pos will not be std::string::npos.
	std::string ret;
	char buffer[3];
	State state;
	if (allow_whitespace) {
		pos = src.find_first_not_of(" \t\r\n", pos);
		if (pos == std::string::npos)
			pos = src.size();
	}
	if (pos > src.size()) {
		pos = std::string::npos;
		return {};
	}
	if (pos == src.size())
		return {};
	while (true) {
		// (Maybe) allow whitespace between units.
		if (allow_whitespace) {
			pos = src.find_first_not_of(" \t\r\n", pos);
			if (pos == std::string::npos) {
				pos = src.size();
				return ret;
			}
		}

		// Check if we are done.
		if (pos == src.size())
			return ret;

		// Check that there are enough characters for a unit.
		if (!(pos + 4 <= src.size())) {
			return ret;
		}

		// If the next character is not a valid base64 letter, finish.
		if (fromalphabet(src[pos]) < 0)
			return ret;

		// Decode the unit.
		decode(src, pos, buffer, state);

		// Handle result.
		switch (state) {
		case GOOD:	// Unit has been decoded normally; store it and continue.
			ret += std::string(buffer, 3);
			break;
		case PAD1:	// Unit contained padding; store it and return.
			return ret + std::string(buffer, 2);
		case PAD2:	// Unit contained padding; store it and return.
			return ret + std::string(buffer, 1);
		default:
			assert(state == BAD);
			return {};
		}
	}
} // }}}
// }}}

// Sha1 digest // {{{
static void sha1rotate(uint32_t *a, uint32_t f) { // {{{
	uint32_t temp = ((a[0] << 5) | (a[0] >> (32 - 5))) + a[4] + f;
	a[4] = a[3];
	a[3] = a[2];
	a[2] = (a[1] << 30) | (a[1] >> (32 - 30));
	a[1] = a[0];
	a[0] = temp;
} // }}}

std::string sha1(std::string const &src) { // {{{
	// Constants.
	// Initial hash, in host endian.
	uint32_t h[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};

	// Preprocess the message.
	std::string message = src + std::string("\x80", 1);
	uint64_t belen = htobe64(8 * src.size());
	int pad = 64 - (message.size() + 8) % 64;
	message += std::string(pad, '\0') + std::string(reinterpret_cast <char *>(&belen), 8);
	assert(message.size() % 64 == 0);

	// Handle chunks.
	std::string::size_type pos = 0;
	while (pos < message.size()) {
		// Extract data.
		uint32_t w[80];
		for (int i = 0; i < 16; ++i) {
			w[i] = be32toh(*reinterpret_cast <uint32_t *>(&message.data()[pos]));
			pos += 4;
		}

		// Generate extra words.
		for (int i = 16; i < 80; ++i) {
			uint32_t b = (w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]);
			w[i] = (b << 1) | (b >> (32 - 1));
		}

		// Initialize working values.
		uint32_t a[5];
		for (int i = 0; i < 5; ++i)
			a[i] = h[i];

		// Main loop
		for (int i = 0; i < 20; ++i) {
			uint32_t f = (a[1] & a[2]) | (~a[1] & a[3]);
			uint32_t k = 0x5a827999;
			sha1rotate(a, f + k + w[i]);
		}
		for (int i = 20; i < 40; ++i) {
			uint32_t f = a[1] ^ a[2] ^ a[3];
			uint32_t k = 0x6ed9eba1;
			sha1rotate(a, f + k + w[i]);
		}
		for (int i = 40; i < 60; ++i) {
			uint32_t f = (a[1] & a[2]) | (a[1] & a[3]) | (a[2] & a[3]);
			uint32_t k = 0x8f1bbcdc;
			sha1rotate(a, f + k + w[i]);
		}
		for (int i = 60; i < 80; ++i) {
			uint32_t f = a[1] ^ a[2] ^ a[3];
			uint32_t k = 0xca62c1d6;
			sha1rotate(a, f + k + w[i]);
		}

		// Add result to hash.
		for (int i = 0; i < 5; ++i)
			h[i] += a[i];
	}
	//Result: hh = (h0 leftshift 128) or (h1 leftshift 96) or (h2 leftshift 64) or (h3 leftshift 32) or h4
	uint32_t hh[5];
	for (int i = 0; i < 5; ++i)
		hh[i] = htobe32(h[i]);
	return std::string(reinterpret_cast <char *>(hh), 20);
} // }}}
// }}}

// Logging. {{{
std::ostream *log_output = &std::cerr;
bool log_date = false;

void set_log_output(std::ostream &target) { // {{{
	/* Change target for WL_log().
	By default, WL_log() sends its output to standard error.  This function is
	used to change the target.
	@param file: The new file to write WL_log output to.
	@return None.
	*/
	log_output = &target;
	log_date = true;
} // }}}

void log_impl(std::string const &message, std::string const &filename, std::string const &funcname, int line) { // {{{
	/* Log a message.
	Write a message to WL_log (default standard error, can be changed with
	set_log_output()).  A timestamp is added before the message and a
	newline is added to it.
	@param message: The message to WL_log. Multiple arguments are logged on separate lines. Newlines in arguments cause the message to be split, so they should not contain a closing newline.
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

}

// vim: set foldmethod=marker :

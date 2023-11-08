#include "webloop/webobject.hh"
#include "webloop/network.hh"
#include <cmath>

namespace Webloop {

std::shared_ptr <WebNone> WebNone::instance;

std::string WebFloat::dump() const { // {{{
#ifdef WEBOBJECT_DUMPS_JSON
	if (std::isnan(value))
		return "NaN";
	if (std::isinf(value)) {
		if (value < 0)
			return "-Infinity";
		return "Infinity";
	}
	return std::to_string(value);
#else
	ieee754_double v = {.d = value};
	uint64_t mantissa = htole64((uint64_t(v.ieee.mantissa1) << 20) | v.ieee.mantissa0);
	uint16_t exponent = htole16(v.ieee.exponent | (v.ieee.negative ? (1 << 14) : 0));
	return "F" + std::string(reinterpret_cast <char const *>(&mantissa), sizeof(uint64_t)) + std::string(reinterpret_cast <char const *>(&exponent), sizeof(uint16_t));
#endif
} // }}}

std::string WebString::dump() const { // {{{
#ifdef WEBOBJECT_DUMPS_JSON
	std::string ret = "\"";
	for (auto c: value) {
		if (c != '\\' && c >= 0x20 && c <= 0x7e)
			ret += c;
		else {
			ret += "\\x" + (std::ostringstream() << std::hex << std::setw(2) << std::setfill('0') << unsigned(c & 0xff)).str();
		}
	}
	ret += "\"";
	return ret;
#else
	return "S" + value;
#endif
} // }}}

std::string WebVector::dump() const { // {{{
#ifdef WEBOBJECT_DUMPS_JSON
	std::string ret = "[";
	std::string sep = "";
	for (auto i: value) {
		ret += sep;
		ret += i->dump();
		sep = ",";
	}
	return ret + "]";
#else
	std::string ret = "V" + WebInt(value.size()).dump();
	for (auto i: value) {
		std::string e = i->dump();
		ret += WebInt(e.size()).dump();
		ret += std::move(e);
	}
	return ret;
#endif
} // }}}

std::string WebVector::print() const { // {{{
	std::string ret = "[ ";
	for (auto i: value)
		ret += i->print() + ", ";
	ret += "]";
	return ret;
} // }}}

std::string WebMap::dump() const { // {{{
#ifdef WEBOBJECT_DUMPS_JSON
	std::string ret = "{";
	std::string sep = "";
	for (auto i: value) {
		ret += sep;
		ret += WebString(i.first).dump() + ":" + i.second->dump();
		sep = ",";
	}
	return ret + "}";
#else
	std::string ret = "M" + WebInt(value.size()).dump();
	for (auto i: value) {
		std::string const &key = i.first;
		ret += WebInt(key.size()).dump();
		ret += key;

		std::string v = i.second->dump();
		ret += WebInt(v.size()).dump();
		ret += std::move(v);
	}
	return ret;
#endif
} // }}}

std::string WebMap::print() const { // {{{
	std::string ret = "{ ";
	for (auto i: value) {
		ret += i.first + ": " + i.second->print() + ", ";
	}
	ret += "}";
	return ret;
} // }}}

#ifdef WEBOBJECT_DUMPS_JSON
static int parse_digit(char d) { // {{{
	if (d >= '0' && d <= '9')
		return d - '0';
	if (d >= 'a' && d <= 'f')
		return d - 'a' + 10;
	WL_log("invalid hex digit " + std::string(&d, 1));
	return 0;
} // }}}

static std::string read_string(std::string const &data, std::string::size_type &pos) { // {{{
	// Read a string.
	// Pos is updated to position after final '"'.
	// data[pos] must initially be '"'.
	std::string ret;
	++pos;
	while (true) {
		auto p = data.find_first_of("\\\"", pos);
		if (p == std::string::npos) {
			WL_log("unfinished string");
			ret += data.substr(pos);
			pos = std::string::npos;
			return ret;
		}
		ret += data.substr(pos, p - pos);
		pos = p;

		if (data[pos] == '"') {
			++pos;
			return ret;
		}

		assert(data[pos] == '\\');
		++pos;
		if (pos >= data.size()) {
			WL_log("unfinished string");
			pos = std::string::npos;
			return ret;
		}
		switch (data[pos]) {
		case '\\':
		case '"':
			ret += data[p];
			break;
		case 'n':
			ret += "\n";
			break;
		case 'r':
			ret += "\r";
			break;
		case 'v':
			ret += "\v";
			break;
		case 't':
			ret += "\t";
			break;
		case 'f':
			ret += "\r";
			break;
		case 'a':
			ret += "\a";
			break;
		case 'x':
		{
			if (pos + 3 >= data.size()) {
				WL_log("unfinished string");
				ret += "x";
			}
			else {
				int d1 = parse_digit(data[pos + 1]);
				int d2 = parse_digit(data[pos + 2]);
				ret += char(d1 << 4 | d2);
				pos += 2;
			}
			break;
		}
		default:
			WL_log("unrecognized escape sequence in JSON string: " + data.substr(pos - 1, 2));
			ret += data[pos];
			break;
		}
		++pos;
	}
} // }}}

static std::shared_ptr <WebObject> load_item(std::string const &data, std::string::size_type &pos) { // {{{
	pos = data.find_first_not_of(" \t\n\r\v\f", pos);
	if (pos == std::string::npos)
		return WebNone::create();
	if (startswith(data, "null", pos)) {
		pos += 4;
		return WebNone::create();
	}
	if (startswith(data, "false", pos)) {
		pos += 5;
		return WebBool::create(false);
	}
	if (startswith(data, "true", pos)) {
		pos += 4;
		return WebBool::create(true);
	}
	if (startswith(data, "NaN", pos)) {
		pos += 3;
		return WebFloat::create(NAN);
	}
	if (startswith(data, "-Infinity", pos)) {
		pos += 9;
		return WebFloat::create(-INFINITY);
	}
	if (startswith(data, "Infinity", pos)) {
		pos += 8;
		return WebFloat::create(INFINITY);
	}
	switch(data[pos]) {
	case '[': // vector
	{
		auto vector = WebVector::create();
		pos = data.find_first_not_of(" \t\n\r\v\f", pos + 1);
		if (pos == std::string::npos) {
			WL_log("incomplete vector");
			return vector;
		}
		if (data[pos] == ']') {
			++pos;
			return vector;
		}
		while (true) {
			auto item = load_item(data, pos);
			vector->push_back(item);
			if (pos == std::string::npos || pos >= data.size()) {
				WL_log("incomplete vector");
				pos = std::string::npos;
				return vector;
			}
			pos = data.find_first_not_of(" \t\n\r\v\f", pos);
			if (pos == std::string::npos) {
				WL_log("incomplete vector");
				return vector;
			}
			if (data[pos] == ']') {
				++pos;
				return vector;
			}
			if (data[pos] != ',')
				WL_log("expected ',' after vector item");
			pos = data.find_first_not_of(" \t\n\r\v\f", pos + 1);
			if (pos == std::string::npos) {
				WL_log("incomplete vector");
				return vector;
			}
		}
	}
	case '{': // map
	{
		auto map = WebMap::create();
		pos = data.find_first_not_of(" \t\n\r\v\f", pos + 1);
		if (pos == std::string::npos) {
			WL_log("incomplete map");
			return map;
		}
		if (data[pos] == '}') {
			++pos;
			return map;
		}
		while (true) {
			if (data[pos] != '"')
				WL_log("no string as map key");
			std::string key = read_string(data, pos);
			pos = data.find_first_not_of(" \t\n\r\v\f", pos);
			if (pos == std::string::npos) {
				WL_log("incomplete map");
				return map;
			}
			if (data[pos] != ':')
				WL_log("':' expected after map key");
			pos = data.find_first_not_of(" \t\n\r\v\f", pos + 1);
			if (pos == std::string::npos) {
				WL_log("incomplete map");
				return map;
			}
			auto item = load_item(data, pos);
			(*map)[key] = item;
			pos = data.find_first_not_of(" \t\n\r\v\f", pos);
			if (pos == std::string::npos) {
				WL_log("incomplete map");
				return map;
			}
			if (data[pos] == '}') {
				++pos;
				return map;
			}
			if (data[pos] != ',')
				WL_log("expected ',' after map item");
			pos = data.find_first_not_of(" \t\n\r\v\f", pos + 1);
			if (pos == std::string::npos) {
				WL_log("incomplete map");
				return map;
			}
		}
	}
	case '"': // string
	{
		return WebString::create(read_string(data, pos));
	}
	default: // int or float
	{
		auto pi = data.find_first_not_of("0123456789-+", pos);
		auto pf = data.find_first_not_of("0123456789-+.e", pos);
		if (pi == pf) {
			// This is an int.
			WebObject::IntType i;
			std::istringstream s(data.substr(pos, pi - pos));
			s >> i;
			if (std::string::size_type(s.tellg()) < pi)
				WL_log("junk in JSON int");
			pos = pi;
			return WebInt::create(i);
		}
		else {
			// This is a float.
			WebObject::FloatType f;
			std::istringstream s(data.substr(pos, pf - pos));
			s >> f;
			if (std::string::size_type(s.tellg()) < pf)
				WL_log("junk in JSON int");
			pos = pf;
			return WebFloat::create(f);
		}
	}
	}
} // }}}

std::shared_ptr <WebObject> WebObject::load(std::string const &data) { // {{{
	assert(!data.empty());
	std::string::size_type pos = 0;
	return load_item(data, pos);
} // }}}
#else
void WebFloat::load_impl(std::string const &data) { // {{{
	assert(data.length() == 1 + sizeof(uint64_t) + sizeof(uint16_t));
	ieee754_double v;
	uint64_t mantissa = le64toh(*reinterpret_cast <uint64_t const *>(&data.data()[1]));
	v.ieee.mantissa0 = mantissa & ((1 << 20) - 1);
	v.ieee.mantissa1 = mantissa >> 20;
	uint16_t exponent = le16toh(*reinterpret_cast <uint16_t const *>(&data.data()[1 + sizeof(uint64_t)]));
	v.ieee.negative = (exponent >> 14) & 1;
	v.ieee.exponent = exponent & ((1 << 11) - 1);
	value = v.d;
} // }}}

void WebVector::load_impl(std::string const &data) { // {{{
	value.clear();
	WebInt num;
	assert(data.size() >= 1 + 9);
	num.load(data.substr(1, 9));
	std::string::size_type pos = 1 + 9;
	for (ssize_t i = 0; i < int64_t(num); ++i) {
		WebInt l;
		assert(data.size() >= pos + 9);
		l.load(data.substr(pos, 9));
		pos += 9;
		size_t len = int64_t(l);
		assert(data.size() >= pos + len);
		value.push_back(load(data.substr(pos, len)));
		pos += len;
	}
} // }}}

void WebMap::load_impl(std::string const &data) { // {{{
	value.clear();
	WebInt num;
	assert(data.size() >= 1 + 9);
	num.load(data.substr(1, 9));
	std::string::size_type pos = 1 + 9;
	for (ssize_t i = 0; i < int64_t(num); ++i) {
		WebInt l;
		assert(data.size() >= pos + 9);
		l.load(data.substr(pos, 9));
		pos += 9;
		size_t len = int64_t(l);
		assert(data.size() >= pos + len);
		std::string key = data.substr(pos, len);
		pos += len;

		assert(data.size() >= pos + 9);
		l.load(data.substr(pos, 9));
		pos += 9;
		len = int64_t(l);
		assert(data.size() >= pos + len);
		value[key] = load(data.substr(pos, len));
		pos += len;
	}
} // }}}

std::shared_ptr <WebObject> WebObject::load(std::string const &data) { // {{{
	assert(!data.empty());
	WebObject *ret;
	switch(data[0]) {
	case 'N': // None
		ret = new WebNone();
		break;
	case 'F': // Bool: false
		ret = new WebBool(false);
		break;
	case 'T': // Bool: true
		ret = new WebBool(true);
		break;
	case 'I': // Int
		ret = new WebInt();
		break;
	case 'F': // Float
		ret = new WebFloat();
		break;
	case 'S': // String
		ret = new WebString();
		break;
	case 'V': // Vector
		ret = new WebVector();
		break;
	case 'M': // Map
		ret = new WebMap();
		break;
	default:
		throw "Invalid type in serialized string";
	}
	ret->load_impl(data);
	return std::shared_ptr <WebObject> (ret);
} // }}}
#endif

}

// vim: set foldmethod=marker :

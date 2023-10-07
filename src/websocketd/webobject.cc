#include "webobject.hh"

std::shared_ptr <WebNone> WebNone::instance;

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

#ifdef WEBOBJECT_DUMPS_JSON
std::string WebVector::dump() const { // {{{
	std::string ret = "[";
	std::string sep = "";
	for (auto i: value) {
		ret += sep;
		ret += i->dump();
		sep = ",";
	}
	return ret + "]";
} // }}}
#else
std::string WebVector::dump() const { // {{{
	std::string ret = "V" + WebInt(value.size()).dump();
	for (auto i: value) {
		std::string e = i->dump();
		ret += WebInt(e.size()).dump();
		ret += std::move(e);
	}
	return ret;
} // }}}
#endif

std::string WebVector::print() const { // {{{
	std::string ret = "[ ";
	for (auto i: value)
		ret += i->print() + ", ";
	ret += "]";
	return ret;
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

#ifdef WEBOBJECT_DUMPS_JSON
std::string WebMap::dump() const { // {{{
	std::string ret = "{";
	std::string sep = "";
	for (auto i: value) {
		ret += sep;
		ret += WebString(i.first).dump() + ":" + i.second->dump();
		sep = ",";
	}
	return ret + "}";
} // }}}
#else
std::string WebMap::dump() const { // {{{
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
} // }}}
#endif

std::string WebMap::print() const { // {{{
	std::string ret = "{ ";
	for (auto i: value) {
		ret += i.first + ": " + i.second->print() + ", ";
	}
	ret += "}";
	return ret;
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

// vim: set foldmethod=marker :

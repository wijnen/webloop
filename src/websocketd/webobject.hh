#ifndef _WEBOBJECT_HH
#define _WEBOBJECT_HH

// Includes. {{{
#include <coroutine>
#include <string>
#include <iostream>
#include <vector>
#include <map>
#include <cassert>
#include <endian.h>
#include <ieee754.h>
#include <sstream>
#include <memory>
// }}}

#define WEBSOCKET_DUMPS_JSON

//#define STARTFUNC do { std::cout << "Function start: " << __LINE__ << ": " << __FUNCTION__ << std::endl; } while(0)
#define STARTFUNC do {} while(0)

class WebNone;
class WebInt;
class WebFloat;
class WebString;
class WebVector;
class WebMap;

class WebObject { // {{{
public:		// Types
	enum Type { NONE, INT, FLOAT, STRING, VECTOR, MAP };
	typedef std::vector <std::shared_ptr <WebObject> > VectorType;
	typedef std::map <std::string, std::shared_ptr <WebObject> > MapType;
	typedef int64_t IntType;
private:	// Data members
	Type const type;
public:		// Member functions
	WebObject(Type type) : type(type) {}

	WebNone *as_none() { assert(type == NONE); return reinterpret_cast <WebNone *>(this); }
	WebInt *as_int() { assert(type == INT); return reinterpret_cast <WebInt *>(this); }
	WebFloat *as_float() { assert(type == FLOAT); return reinterpret_cast <WebFloat *>(this); }
	WebString *as_string() { assert(type == STRING); return reinterpret_cast <WebString *>(this); }
	WebVector *as_vector() { assert(type == VECTOR); return reinterpret_cast <WebVector *>(this); }
	WebMap *as_map() { assert(type == MAP); return reinterpret_cast <WebMap *>(this); }

	WebObject &operator=(WebObject const &other) = delete;
	WebObject(WebObject const &other) : type(other.type) {}
	WebObject(WebObject &&other) : type(other.type) {}
	virtual ~WebObject() {}
	constexpr Type get_type() const { return type; }

	// Deep copy; this makes a copy of the derived class, returns a new() WebObject.
	virtual std::shared_ptr <WebObject> copy() const = 0;

	// Serialization.
	virtual std::string dump() const = 0;
	virtual std::string print() const = 0;
	static std::shared_ptr <WebObject> load(std::string const &data);
	friend std::ostream &operator<<(std::ostream &s, WebObject const &o) { s << o.print(); return s; }
private:
	virtual void load_impl(std::string const &data) = 0;
}; // }}}

// Object class definitions.
class WebNone : public WebObject { // {{{
	void load_impl(std::string const &data) override { assert(data.length() == 1); }
	static std::shared_ptr <WebNone> instance; // While it is allowed to construct more of them, create() will always return this one.
public:
	WebNone() : WebObject(NONE) {}
	static std::shared_ptr <WebNone> create() { return instance; }
	std::shared_ptr <WebObject> copy() const override { return std::shared_ptr <WebObject> (new WebNone()); }
	std::string print() const override { return "[None]"; }
#ifdef WEBOBJECT_DUMPS_JSON
	std::string dump() const override { return "null"; }
#else
	std::string dump() const override { return "N"; }
#endif
}; // }}}

class WebInt : public WebObject { // {{{
	friend class WebString;
	friend class WebVector;
	friend class WebMap;
	IntType value;
	void load_impl(std::string const &data) override {
		assert(data.length() == 1 + sizeof(uint64_t));
		uint64_t v = *reinterpret_cast <uint64_t const *>(&data.data()[1]);
		value = le64toh(v);
	}
public:
	WebInt() : WebObject(INT), value() {}
	WebInt(IntType value) : WebObject(INT), value(value) {}
	static std::shared_ptr <WebInt> create(IntType value) { return std::shared_ptr <WebInt> (new WebInt(value)); }
	std::shared_ptr <WebObject> copy() const override { return std::shared_ptr <WebObject> (new WebInt(*this)); }
	operator IntType() const { return value; }
	operator IntType &() { return value; }
	std::string print() const override { return (std::ostringstream() << value).str(); }
#ifdef WEBOBJECT_DUMPS_JSON
	std::string dump() const override { std::ostringstream s(); s << value; return s.str(); }
#else
	std::string dump() const override { uint64_t v = htole64(value); return "I" + std::string(reinterpret_cast <char const *>(&v), sizeof(uint64_t)); }
#endif
}; // }}}

class WebFloat : public WebObject { // {{{
	double value;
#ifdef WEBOBJECT_DUMPS_JSON
	// TODO: Handle inf and nan.
	std::string dump() const override { std::ostringstream s(); s << value; return s.str(); }
#else
	std::string dump() const override {
		ieee754_double v = {.d = value};
		uint64_t mantissa = htole64((uint64_t(v.ieee.mantissa1) << 20) | v.ieee.mantissa0);
		uint16_t exponent = htole16(v.ieee.exponent | (v.ieee.negative ? (1 << 14) : 0));
		return "F" + std::string(reinterpret_cast <char const *>(&mantissa), sizeof(uint64_t)) + std::string(reinterpret_cast <char const *>(&exponent), sizeof(uint16_t));
	}
#endif
	std::string print() const override { return (std::ostringstream() << value).str(); }
	void load_impl(std::string const &data) override;
public:
	WebFloat() : WebObject(FLOAT), value() {}
	WebFloat(double value) : WebObject(FLOAT), value(value) {}
	static std::shared_ptr <WebFloat> create(double value) { return std::shared_ptr <WebFloat> (new WebFloat(value)); }
	std::shared_ptr <WebObject> copy() const override { return std::shared_ptr <WebObject> (new WebFloat(*this)); }
	operator double() const { return value; }
	operator double &() { return value; }
}; // }}}

class WebString : public WebObject { // {{{
	std::string value;
#ifdef WEBOBJECT_DUMPS_JSON
	std::string dump() const override {
		std::string ret = "\"";
		for (auto c: value) {
			if (c != '\\' && c >= 0x20 && c <= 0x7e)
				ret += c;
			else {
				std::ostringstream s;
				s << std::hex << std::fill('0') << c;
				ret += "\\x" + s.str();
			}
		}
		ret += "\"";
		return ret;
	}
#else
	std::string dump() const override { return "S" + value; }
#endif
	std::string print() const override { return "\"" + value + "\""; }
	void load_impl(std::string const &data) override { value = data.substr(1); }
public:
	WebString() : WebObject(STRING), value() {}
	WebString(std::string const &value) : WebObject(STRING), value(value) {}
	static std::shared_ptr <WebString> create(std::string const &value) { return std::shared_ptr <WebString> (new WebString(value)); }
	std::shared_ptr <WebObject> copy() const override { return std::shared_ptr <WebObject> (new WebString(*this)); }
	WebString(std::string &&value) : WebObject(STRING), value(value) {}
	operator std::string const &() const { return value; }
	operator std::string &() { return value; }
}; // }}}

class WebVector : public WebObject { // {{{
	VectorType value;
	std::string dump() const override;
	std::string print() const override;
	void load_impl(std::string const &data) override;
public:
	WebVector() : WebObject(VECTOR), value() {}
	WebVector(WebVector const &other) : WebObject(VECTOR), value() { for (auto i: other.value) value.push_back(i->copy()); }
	WebVector(WebVector &&other) : WebObject(VECTOR), value(other.value) { other.value.clear(); }
	template <typename... Ts> WebVector(Ts... args) : WebObject(VECTOR), value() { value.assign({args...}); }
	template <typename... Ts> static std::shared_ptr <WebVector> create(Ts... args) { return std::shared_ptr <WebVector> (new WebVector(args...)); }
	std::shared_ptr <WebObject> copy() const override { return std::shared_ptr <WebObject> (new WebVector(*this)); }
	//operator VectorType const &() const { return value; }
	//operator VectorType &() { return value; }
	std::shared_ptr <WebObject> &operator[](size_t index) { STARTFUNC; return value[index]; }
	std::shared_ptr <WebObject> const &operator[](size_t index) const { STARTFUNC; return value[index]; }
	~WebVector() override { STARTFUNC; value.clear(); }
	bool empty() const { return value.empty(); }
	size_t size() const { return value.size(); }
}; // }}}

class WebMap : public WebObject { // {{{
	MapType value;
	std::string dump() const override;
	std::string print() const override;
	void load_impl(std::string const &data) override;
public:
	WebMap() : WebObject(MAP), value() {}
	WebMap(WebMap const &other) : WebObject(MAP), value() { for (auto i: other.value) value[i.first] = i.second->copy(); }
	WebMap(WebMap &&other) : WebObject(MAP), value(other.value) { other.value.clear(); }
	template <typename... Ts> WebMap(Ts... args) : WebObject(MAP), value({args...}) {}
	template <typename... Ts> static std::shared_ptr <WebMap> create(Ts... args) { return std::shared_ptr <WebMap> (new WebMap(args...)); }
	std::shared_ptr <WebObject> copy() const override { return std::shared_ptr <WebObject> (new WebMap(*this)); }
	std::shared_ptr <WebObject> &operator[](std::string const &key) { STARTFUNC; return value.at(key); }
	std::shared_ptr <WebObject> const &operator[](std::string const &key) const { STARTFUNC; return value.at(key); }
	~WebMap() override { STARTFUNC; value.clear(); }
}; // }}}

#endif

// vim: set foldmethod=marker :

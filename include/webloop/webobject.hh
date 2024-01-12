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
#include <iomanip>
#include "tools.hh"
// }}}

namespace Webloop {

#define WEBOBJECT_DUMPS_JSON

class WebNone;
class WebBool;
class WebInt;
class WebFloat;
class WebString;
class WebVector;
class WebMap;

class WebObject { // {{{
public:		// Types
	enum Type { NONE, BOOL, INT, FLOAT, STRING, VECTOR, MAP };
	typedef std::vector <std::shared_ptr <WebObject> > VectorType;
	typedef std::map <std::string, std::shared_ptr <WebObject> > MapType;
	typedef int64_t IntType;
	typedef double FloatType;
private:	// Data members
	Type const type;
public:		// Member functions
	WebObject(Type type) : type(type) {}

	WebNone *as_none() { assert(type == NONE); return reinterpret_cast <WebNone *>(this); }
	WebBool *as_bool() { assert(type == BOOL); return reinterpret_cast <WebBool *>(this); }
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
#ifndef WEBOBJECT_DUMPS_JSON
	virtual void load_impl(std::string const &data) = 0;
#endif
}; // }}}

class WebHelper { // {{{
	std::shared_ptr <WebObject> data;
public:
	inline WebHelper();
	inline WebHelper(bool src);
	inline WebHelper(WebObject::IntType src);
	inline WebHelper(WebObject::FloatType src);
	inline WebHelper(char const *src);
	inline WebHelper(std::string const &src);
	inline WebHelper(std::shared_ptr <WebNone> src);
	inline WebHelper(std::shared_ptr <WebBool> src);
	inline WebHelper(std::shared_ptr <WebInt> src);
	inline WebHelper(std::shared_ptr <WebFloat> src);
	inline WebHelper(std::shared_ptr <WebString> src);
	inline WebHelper(std::shared_ptr <WebVector> src);
	inline WebHelper(std::shared_ptr <WebMap> src);
	WebHelper(std::shared_ptr <WebObject> src) : data (src) {}
	operator std::shared_ptr <WebObject>() const { return data; }
}; // }}}

// Object class definitions.
class WebNone : public WebObject { // {{{
#ifndef WEBOBJECT_DUMPS_JSON
	void load_impl(std::string const &data) override { assert(data.length() == 1); }
#endif
	static std::shared_ptr <WebNone> instance; // While it is allowed to construct more of them, create() will always return this one.
public:
	WebNone() : WebObject(NONE) {}
	static std::shared_ptr <WebNone> create() { return instance; }
	std::shared_ptr <WebObject> copy() const override { return std::shared_ptr <WebObject> (new WebNone()); }
	std::string print() const override { return "None"; }
#ifdef WEBOBJECT_DUMPS_JSON
	std::string dump() const override { return "null"; }
#else
	std::string dump() const override { return "N"; }
#endif
}; // }}}

class WebBool : public WebObject { // {{{
	friend class WebString;
	friend class WebVector;
	friend class WebMap;
	bool value;
#ifndef WEBOBJECT_DUMPS_JSON
	void load_impl(std::string const &data) override { assert(data.length() == 1); }
#endif
public:
	WebBool() : WebObject(BOOL), value(false) {}
	WebBool(bool value) : WebObject(BOOL), value(value) {}
	static std::shared_ptr <WebBool> create(bool value) { return std::shared_ptr <WebBool> (new WebBool(value)); }
	std::shared_ptr <WebObject> copy() const override { return std::shared_ptr <WebObject> (new WebBool(*this)); }
	operator bool() const { return value; }
	operator bool &() { return value; }
	std::string print() const override { return value ? "true" : "false"; }
#ifdef WEBOBJECT_DUMPS_JSON
	std::string dump() const override { return value ? "true" : "false"; }
#else
	std::string dump() const override { return value ? "T" : "F"; }
#endif
}; // }}}

class WebInt : public WebObject { // {{{
	friend class WebString;
	friend class WebVector;
	friend class WebMap;
	IntType value;
#ifndef WEBOBJECT_DUMPS_JSON
	void load_impl(std::string const &data) override {
		assert(data.length() == 1 + sizeof(uint64_t));
		uint64_t v = *reinterpret_cast <uint64_t const *>(&data.data()[1]);
		value = le64toh(v);
	}
#endif
public:
	WebInt() : WebObject(INT), value() {}
	WebInt(IntType value) : WebObject(INT), value(value) {}
	static std::shared_ptr <WebInt> create(IntType value) { return std::shared_ptr <WebInt> (new WebInt(value)); }
	std::shared_ptr <WebObject> copy() const override { return std::shared_ptr <WebObject> (new WebInt(*this)); }
	operator IntType() const { return value; }
	operator IntType &() { return value; }
	std::string print() const override { return (std::ostringstream() << value).str(); }
#ifdef WEBOBJECT_DUMPS_JSON
	std::string dump() const override { return (std::ostringstream() << value).str(); }
#else
	std::string dump() const override { uint64_t v = htole64(value); return "I" + std::string(reinterpret_cast <char const *>(&v), sizeof(uint64_t)); }
#endif
}; // }}}

class WebFloat : public WebObject { // {{{
	FloatType value;
#ifndef WEBOBJECT_DUMPS_JSON
	void load_impl(std::string const &data) override;
#endif
public:
	std::string dump() const override;
	std::string print() const override { return (std::ostringstream() << value).str(); }
	WebFloat() : WebObject(FLOAT), value() {}
	WebFloat(FloatType value) : WebObject(FLOAT), value(value) {}
	static std::shared_ptr <WebFloat> create(FloatType value) { return std::shared_ptr <WebFloat> (new WebFloat(value)); }
	std::shared_ptr <WebObject> copy() const override { return std::shared_ptr <WebObject> (new WebFloat(*this)); }
	operator FloatType() const { return value; }
	operator FloatType &() { return value; }
}; // }}}

class WebString : public WebObject { // {{{
	std::string value;
#ifndef WEBOBJECT_DUMPS_JSON
	void load_impl(std::string const &data) override { value = data.substr(1); }
#endif
public:
	std::string dump() const override;
	std::string print() const override { return "\"" + value + "\""; }
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
#ifndef WEBOBJECT_DUMPS_JSON
	void load_impl(std::string const &data) override;
#endif
public:
	std::string dump() const override;
	std::string print() const override;
	WebVector() : WebObject(VECTOR), value() {}
	WebVector(WebVector const &other) : WebObject(VECTOR), value() { for (auto i: other.value) value.push_back(i->copy()); }
	WebVector(WebVector &&other) : WebObject(VECTOR), value(other.value) { other.value.clear(); }
	template <typename... Ts> WebVector(Ts... args) : WebObject(VECTOR), value() { value.assign({args...}); }
	template <typename... Ts> static std::shared_ptr <WebVector> create(Ts... args) { return std::shared_ptr <WebVector> (new WebVector(args...)); }
	WebVector(std::initializer_list <WebHelper> args) : WebObject(VECTOR), value() { for (unsigned i = 0; i < args.size(); ++i) value.push_back(args.begin()[i]); }
	static std::shared_ptr <WebVector> build(std::initializer_list <WebHelper> args) { return std::shared_ptr <WebVector> (new WebVector(args)); }
	std::shared_ptr <WebObject> copy() const override { return std::shared_ptr <WebObject> (new WebVector(*this)); }
	//operator VectorType const &() const { return value; }
	//operator VectorType &() { return value; }
	std::shared_ptr <WebObject> &operator[](size_t index) { STARTFUNC; return value[index]; }
	std::shared_ptr <WebObject> const &operator[](size_t index) const { STARTFUNC; return value[index]; }
	~WebVector() override { STARTFUNC; value.clear(); }
	bool empty() const { return value.empty(); }
	size_t size() const { return value.size(); }
	void push_back(std::shared_ptr <WebObject> item) { value.push_back(item); }
	void pop_back() { value.pop_back(); }
	void insert(size_t position, std::shared_ptr <WebObject> item) { value.insert(value.begin() + position, item); }
}; // }}}

class WebMap : public WebObject { // {{{
	MapType value;
#ifndef WEBOBJECT_DUMPS_JSON
	void load_impl(std::string const &data) override;
#endif
public:
	std::string dump() const override;
	std::string print() const override;
	WebMap() : WebObject(MAP), value() {}
	WebMap(WebMap const &other) : WebObject(MAP), value() { for (auto i: other.value) value[i.first] = i.second->copy(); }
	WebMap(WebMap &&other) : WebObject(MAP), value(other.value) { other.value.clear(); }
	template <typename... Ts> WebMap(Ts... args) : WebObject(MAP), value({args...}) {}
	template <typename... Ts> static std::shared_ptr <WebMap> create(Ts... args) { return std::shared_ptr <WebMap> (new WebMap(args...)); }
	WebMap(std::initializer_list <std::tuple <std::string, WebHelper> > args) : WebObject(MAP), value() { for (unsigned i = 0; i < args.size(); ++i) value.insert(std::pair <std::string, std::shared_ptr <WebObject> > (std::get <0> (args.begin()[i]), std::get <1> (args.begin()[i]))); }
	static std::shared_ptr <WebMap> build(std::initializer_list <std::tuple <std::string, WebHelper> > args) { return std::shared_ptr <WebMap> (new WebMap(args)); }
	std::shared_ptr <WebObject> copy() const override { return std::shared_ptr <WebObject> (new WebMap(*this)); }
	std::shared_ptr <WebObject> &operator[](std::string const &key) { STARTFUNC; return value[key]; }
	std::shared_ptr <WebObject> const &operator[](std::string const &key) const { STARTFUNC; return value.at(key); }
	size_t size() const { STARTFUNC; return value.size(); }
	~WebMap() override { STARTFUNC; value.clear(); }
}; // }}}

// WebHelper constructors. {{{
WebHelper::WebHelper() : data(dynamic_pointer_cast <WebObject> (WebNone::create())) {}
WebHelper::WebHelper(bool src) : data(dynamic_pointer_cast <WebObject> (WebBool::create(src))) {}
WebHelper::WebHelper(WebObject::IntType src) : data(dynamic_pointer_cast <WebObject> (WebInt::create(src))) {}
WebHelper::WebHelper(WebObject::FloatType src) : data(dynamic_pointer_cast <WebObject> (WebFloat::create(src))) {}
WebHelper::WebHelper(char const *src) : data(dynamic_pointer_cast <WebObject> (WebString::create(src))) {}
WebHelper::WebHelper(std::string const &src) : data(dynamic_pointer_cast <WebObject> (WebString::create(src))) {}
WebHelper::WebHelper(std::shared_ptr <WebNone> src) : data(dynamic_pointer_cast <WebObject> (src)) {}
WebHelper::WebHelper(std::shared_ptr <WebBool> src) : data(dynamic_pointer_cast <WebObject> (src)) {}
WebHelper::WebHelper(std::shared_ptr <WebInt> src) : data(dynamic_pointer_cast <WebObject> (src)) {}
WebHelper::WebHelper(std::shared_ptr <WebFloat> src) : data(dynamic_pointer_cast <WebObject> (src)) {}
WebHelper::WebHelper(std::shared_ptr <WebString> src) : data(dynamic_pointer_cast <WebObject> (src)) {}
WebHelper::WebHelper(std::shared_ptr <WebVector> src) : data(dynamic_pointer_cast <WebObject> (src)) {}
WebHelper::WebHelper(std::shared_ptr <WebMap> src) : data(dynamic_pointer_cast <WebObject> (src)) {}
// }}}

// Literals for base types. These can only be used with "using namespace Webloop".
static inline std::shared_ptr <WebBool> operator ""_wb(char const *src, size_t size) {
	(void)&size;
	assert((size == 4 && src[0] == 't' && src[1] == 'r' && src[2] == 'u' && src[3] == 'e') || (size == 5 && src[0] == 'f' && src[1] == 'a' && src[2] == 'l' && src[3] == 's' && src[4] == 'e'));
	return WebBool::create(src[0] == 't');
}
static inline std::shared_ptr <WebInt> operator ""_wi(unsigned long long src) { return WebInt::create(src); }
static inline std::shared_ptr <WebFloat> operator ""_wf(long double src) { return WebFloat::create(src); }
static inline std::shared_ptr <WebString> operator ""_ws(char const *src) { return WebString::create(src); }

// Short form to create a WebNone.
static inline std::shared_ptr <WebNone> WN() { return WebNone::create(); }

// Sort form to create a tuple, used by WM().
static inline std::tuple <std::string, WebHelper> WT(std::string const &key, WebHelper &&data) { return {key, data}; }

// Short form to create a WebVector.
template <typename... Ts> inline std::shared_ptr <WebVector> WV(Ts... args) { return WebVector::build({args...}); }

// Short form to create a WebMap.
template <typename... Ts> inline std::shared_ptr <WebMap> WM(Ts... args) { return WebMap::build({args...}); }

}

#endif

// vim: set foldmethod=marker :

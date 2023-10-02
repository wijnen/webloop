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

//#define STARTFUNC do { std::cout << "Function start: " << __LINE__ << ": " << __FUNCTION__ << std::endl; } while(0)
#define STARTFUNC do {} while(0)

// Web Objects. {{{
class WebObject { // {{{
public:		// Types
	enum Type { NONE, INT, FLOAT, STRING, VECTOR, MAP };
	typedef std::vector <std::shared_ptr <WebObject> > VectorType;
	typedef std::map <std::string, std::shared_ptr <WebObject> > MapType;
private:	// Data members
	Type const type;
public:		// Member functions
	WebObject(Type type) : type(type) {}

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
private:
	virtual void load_impl(std::string const &data) = 0;
}; // }}}

// Object class definitions. {{{
class WebNone : public WebObject { // {{{
	virtual std::string dump() const { return "N"; }
	virtual std::string print() const { return "[None]"; }
	virtual void load_impl(std::string const &data) { assert(data.length() == 1); }
public:
	WebNone() : WebObject(NONE) {}
	static std::shared_ptr <WebNone> create() { return std::shared_ptr <WebNone> (new WebNone()); }
	virtual std::shared_ptr <WebObject> copy() const { return std::shared_ptr <WebObject> (new WebNone()); }
}; // }}}

class WebInt : public WebObject { // {{{
	friend class WebString;
	friend class WebVector;
	friend class WebMap;
	int64_t value;
	virtual std::string dump() const { uint64_t v = htole64(value); return "I" + std::string(reinterpret_cast <char const *>(&v), sizeof(uint64_t)); }
	virtual std::string print() const { return (std::ostringstream() << value).str(); }
	virtual void load_impl(std::string const &data) {
		assert(data.length() == 1 + sizeof(uint64_t));
		uint64_t v = *reinterpret_cast <uint64_t const *>(&data.data()[1]);
		value = le64toh(v);
	}
public:
	WebInt() : WebObject(INT), value() {}
	WebInt(int64_t value) : WebObject(INT), value(value) {}
	static std::shared_ptr <WebInt> create(int64_t value) { return std::shared_ptr <WebInt> (new WebInt(value)); }
	virtual std::shared_ptr <WebObject> copy() const { return std::shared_ptr <WebObject> (new WebInt(*this)); }
	operator int64_t() const { return value; }
	operator int64_t &() { return value; }
}; // }}}

class WebFloat : public WebObject { // {{{
	double value;
	virtual std::string dump() const {
		ieee754_double v = {.d = value};
		uint64_t mantissa = htole64((uint64_t(v.ieee.mantissa1) << 20) | v.ieee.mantissa0);
		uint16_t exponent = htole16(v.ieee.exponent | (v.ieee.negative ? (1 << 14) : 0));
		return "F" + std::string(reinterpret_cast <char const *>(&mantissa), sizeof(uint64_t)) + std::string(reinterpret_cast <char const *>(&exponent), sizeof(uint16_t));
	}
	virtual std::string print() const { return (std::ostringstream() << value).str(); }
	virtual void load_impl(std::string const &data) {
		assert(data.length() == 1 + sizeof(uint64_t) + sizeof(uint16_t));
		ieee754_double v;
		uint64_t mantissa = le64toh(*reinterpret_cast <uint64_t const *>(&data.data()[1]));
		v.ieee.mantissa0 = mantissa & ((1 << 20) - 1);
		v.ieee.mantissa1 = mantissa >> 20;
		uint16_t exponent = le16toh(*reinterpret_cast <uint16_t const *>(&data.data()[1 + sizeof(uint64_t)]));
		v.ieee.negative = (exponent >> 14) & 1;
		v.ieee.exponent = exponent & ((1 << 11) - 1);
		value = v.d;
	}
public:
	WebFloat() : WebObject(FLOAT), value() {}
	WebFloat(double value) : WebObject(FLOAT), value(value) {}
	static std::shared_ptr <WebFloat> create(double value) { return std::shared_ptr <WebFloat> (new WebFloat(value)); }
	virtual std::shared_ptr <WebObject> copy() const { return std::shared_ptr <WebObject> (new WebFloat(*this)); }
	operator double() const { return value; }
	operator double &() { return value; }
}; // }}}

class WebString : public WebObject { // {{{
	std::string value;
	virtual std::string dump() const { return "S" + value; }
	virtual std::string print() const { return "\"" + value + "\""; }
	virtual void load_impl(std::string const &data) { value = data.substr(1); }
public:
	WebString() : WebObject(STRING), value() {}
	WebString(std::string const &value) : WebObject(STRING), value(value) {}
	static std::shared_ptr <WebString> create(std::string const &value) { return std::shared_ptr <WebString> (new WebString(value)); }
	virtual std::shared_ptr <WebObject> copy() const { return std::shared_ptr <WebObject> (new WebString(*this)); }
	WebString(std::string &&value) : WebObject(STRING), value(value) {}
	operator std::string const &() const { return value; }
	operator std::string &() { return value; }
}; // }}}

class WebVector : public WebObject { // {{{
	VectorType value;
	virtual std::string dump() const { // {{{
		std::string ret = "V" + WebInt(value.size()).dump();
		for (auto i: value) {
			std::string e = i->dump();
			ret += WebInt(e.size()).dump();
			ret += std::move(e);
		}
		return ret;
	} // }}}
	virtual std::string print() const { // {{{
		std::string ret = "[ ";
		for (auto i: value)
			ret += i->print() + ", ";
		ret += "]";
		return ret;
	} // }}}
	virtual void load_impl(std::string const &data) { // {{{
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
public:
	WebVector() : WebObject(VECTOR), value() {}
	WebVector(WebVector const &other) : WebObject(VECTOR), value() { for (auto i: other.value) value.push_back(i->copy()); }
	WebVector(WebVector &&other) : WebObject(VECTOR), value(other.value) { other.value.clear(); }
	template <typename... Ts> WebVector(Ts... args) : WebObject(VECTOR), value() { value.assign({args...}); }
	template <typename... Ts> static std::shared_ptr <WebVector> create(Ts... args) { return std::shared_ptr <WebVector> (new WebVector(args...)); }
	virtual std::shared_ptr <WebObject> copy() const { return std::shared_ptr <WebObject> (new WebVector(*this)); }
	operator VectorType const &() const { return value; }
	operator VectorType &() { return value; }
	std::shared_ptr <WebObject> &operator[](size_t index) { STARTFUNC; return value[index]; }
	std::shared_ptr <WebObject> const &operator[](size_t index) const { STARTFUNC; return value[index]; }
	virtual ~WebVector() { STARTFUNC; value.clear(); }
}; // }}}

class WebMap : public WebObject { // {{{
	MapType value;
	virtual std::string dump() const { // {{{
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
	virtual std::string print() const { // {{{
		std::string ret = "{ ";
		for (auto i: value) {
			ret += i.first + ": " + i.second->print() + ", ";
		}
		ret += "}";
		return ret;
	} // }}}
	virtual void load_impl(std::string const &data) { // {{{
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
public:
	WebMap() : WebObject(MAP), value() {}
	WebMap(WebMap const &other) : WebObject(MAP), value() { for (auto i: other.value) value[i.first] = i.second->copy(); }
	WebMap(WebMap &&other) : WebObject(MAP), value(other.value) { other.value.clear(); }
	template <typename... Ts> WebMap(Ts... args) : WebObject(MAP), value({args...}) {}
	template <typename... Ts> static std::shared_ptr <WebMap> create(Ts... args) { return std::shared_ptr <WebMap> (new WebMap(args...)); }
	virtual std::shared_ptr <WebObject> copy() const { return std::shared_ptr <WebObject> (new WebMap(*this)); }
	std::shared_ptr <WebObject> &operator[](std::string const &key) { STARTFUNC; return value.at(key); }
	std::shared_ptr <WebObject> const &operator[](std::string const &key) const { STARTFUNC; return value.at(key); }
	virtual ~WebMap() { STARTFUNC; value.clear(); }
}; // }}}
// }}}

// Member (and related) function definitions. {{{
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

std::ostream &operator<<(std::ostream &s, WebObject const &o) { // {{{
	s << o.print();
	return s;
} // }}}
// }}}
// }}}

struct coroutine { // {{{
	struct promise_type;
	using handle_type = std::coroutine_handle <promise_type>;
	struct promise_type {
		std::shared_ptr <WebObject> value;
		std::shared_ptr <WebObject> ret;
		promise_type() : value(nullptr), ret(nullptr) { STARTFUNC; }
		coroutine get_return_object() { STARTFUNC; return coroutine(handle_type::from_promise(*this)); }
		std::suspend_always initial_suspend() noexcept { STARTFUNC; return {}; }
		std::suspend_always final_suspend() noexcept { STARTFUNC; return {}; }
		void unhandled_exception() { STARTFUNC; }
		std::suspend_always yield_value(std::shared_ptr <WebObject> v) { STARTFUNC; value = v; return {}; }
		std::suspend_always return_value(std::shared_ptr <WebObject> v) { STARTFUNC; value = v; return {}; }
		~promise_type() { STARTFUNC; }
	};
	handle_type handle;
	coroutine(handle_type handle) : handle(handle) { STARTFUNC; }
	coroutine(coroutine const &other) = delete;
	coroutine &operator=(coroutine const &other) = delete;
	~coroutine() { STARTFUNC; handle.destroy(); }
	std::shared_ptr <WebObject> &operator()(std::shared_ptr <WebObject> arg) {
		STARTFUNC;
		handle.promise().ret = arg;
		handle();
		return handle.promise().value;
	}
	operator bool() { STARTFUNC; return handle.done(); }
}; // }}}

struct Awaiter { // {{{
	// This class lets "yield" (really co_await Awaiter()) send an argument and return a value.
	// The returned value was allocated using new and needs to be deleted by the caller.
	std::shared_ptr <WebObject> ret;
	coroutine::promise_type *promise;
	std::shared_ptr <WebObject> arg;
	Awaiter(std::shared_ptr <WebObject> arg) : ret(nullptr), arg(arg) { STARTFUNC; }
	bool await_ready() { STARTFUNC; return false; }
	bool await_suspend(coroutine::handle_type handle) {
		STARTFUNC;
		promise = &handle.promise();
		promise->value = arg;
		promise->ret = ret;
		return true;
	}
	std::shared_ptr <WebObject> &await_resume() { STARTFUNC; return promise->ret; }
	~Awaiter() { STARTFUNC; }
}; // }}}

#define Yield(arg) co_await Awaiter(arg)

// Similar to Python's "yield from". Usage:
// YieldFrom(auto, variable_name, coroutine, first_argument);
// std::shared_ptr <WebObject> variable_name; YieldFrom(, variable_name, coroutine, first_argument);
#define YieldFromFull(vardef, var, target, firstarg) \
	vardef var = target(firstarg); \
	while (!bool(target)) { \
		std::shared_ptr <WebObject> next = Yield(var); \
		var = target(next); \
	}
// Syntactic sugar for common case.
#define YieldFrom(var, target) YieldFromFull(auto, var, target, WebNone::create())

// Test program. {{{
coroutine sub() {
	Yield(WebString::create("Sub!"));
	co_return WebString::create("Dom!");
}

coroutine gen(int a) {
	auto o = Yield(WebInt::create(25));
	std::cout << "o = " << o->print() << std::endl;

	auto s = sub();
	YieldFrom(r, s);
	std::cout << "d/s " << r->print() << std::endl;

	auto s2 = sub();
	YieldFromFull(, r, s2, WebNone::create());
	std::cout << "d/s " << r->print() << std::endl;

	co_yield WebString::create(std::string("abc", a));
	co_yield WebInt::create(1);
	co_yield WebVector::create(WebInt::create(4), WebString::create("hoi"), WebFloat::create(3.4));
	co_yield WebMap::create(std::pair <std::string, std::shared_ptr <WebObject> >{"hallo", WebInt::create(3)});
	co_return WebInt::create(1);
}

int main() {
	auto g = gen(2);
	while (!g.handle.done())
		std::cout << *g(WebInt::create(12)) << " -> " << bool(g) << std::endl;
	return 0;
}
// }}}

// vim: set foldmethod=marker :

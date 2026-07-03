#include <webloop/webobject.hh>
#include <iostream>

/*
OUTPUT:
12 + 35 = 47
String: Cheese!
Bool: true
v[]: 18
m[]: string
END OF OUTPUT.
*/

using namespace Webloop;

#define pr(fmt, ...) std::cout << std::format(fmt "\n", ##__VA_ARGS__)

int main(int /*argc*/, char ** /*argv*/)
{
	auto i12 = Webloop::WebInt::create(12);
	auto i35 = Webloop::WebInt::create(35);
	pr("12 + 35 = {}", int(*i12) + *i35);
	auto s = Webloop::WebString::create("Cheese!");
	auto b = Webloop::WebBool::create(true);
	auto n = WN();
	auto v = WV(42, 18, 99, "Yo", false);
	auto m = WM(WT("key1", 15), WT("key2", "string"), WT("key3", true));
	pr("String: {}", std::string(*s));
	pr("Bool: {}", bool(*b));
	pr("v[]: {}", int(*(*v)[1]->as_int()));
	pr("m[]: {}", std::string(*(*m)["key2"]->as_string()));
	return 0;
}

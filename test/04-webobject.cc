#include <webloop/webobject.hh>
#include <iostream>

/*
OUTPUT:
12 + 35 = 47
String: Cheese!
Bool: true
v[]: 18
m[]: string
Func bound: 25
Func arg: 30
Function: 42
Member: attr 10, bound 70, arg 77
Member: 55
END OF OUTPUT.
*/

using namespace Webloop;

#define pr(fmt, ...) std::cout << std::format(fmt "\n", ##__VA_ARGS__)

std::shared_ptr <WebObject> func(std::shared_ptr <WebObject> arg,
		std::shared_ptr <WebObject> bound)
{
	pr("Func bound: {}", int(*bound->as_int()));
	pr("Func arg: {}", int(*arg->as_int()));
	return 42_wi;
}

class MemberTest {
	int attr;
public:
	MemberTest(int a) : attr(a) {}
	std::shared_ptr <WebObject> mem(std::shared_ptr <WebObject> arg,
			std::shared_ptr <WebObject> bound)
	{
		std::cout << std::format("Member: attr {}, bound {}, arg {}",
				attr, int(*bound->as_int()),
				int(*arg->as_int()))
			<< std::endl;
		return 55_wi;
	}
};

int main(int /*argc*/, char ** /*argv*/)
{
	auto i12 = 12_wi;
	auto i35 = 35_wi;
	pr("12 + 35 = {}", int(*i12) + *i35);
	auto s = "Cheese!"_ws;
	auto b = 1_wb;
	auto n = WN();
	auto v = WV(42, 18, 99, "Yo", false);
	auto m = WM(WT("key1", 15), WT("key2", "string"), WT("key3", true));
	pr("String: {}", std::string(*s));
	pr("Bool: {}", bool(*b));
	pr("v[]: {}", int(*(*v)[1]->as_int()));
	pr("m[]: {}", std::string(*(*m)["key2"]->as_string()));
	auto f = WebFunctionPointer::create(&func, 25_wi);
	auto r = (*f)(30_wi);
	pr("Function: {}", WebObject::IntType(*r->as_int()));
	MemberTest mem(10);
	auto mp = WebMemberPointer <MemberTest>::create(&mem, &MemberTest::mem,
			70_wi);
	auto mr = (*mp)(77_wi);
	pr("Member: {}", WebObject::IntType(*mr->as_int()));
	return 0;
}

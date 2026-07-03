#include <webloop/tools.hh>
#include <iostream>

/*
OUTPUT:
strip: 'A 	B'.
lstrip: 'A 	B 	
'.
rstrip: ' 	A 	B'.
start: 'true'.
start: 'false'.
end: 'true'.
end: 'false'.
upper: 'CAMELCASE'.
lower: 'camelcase'.
split: 'one'.
split: 'two	threefour
five '.
split: 'b'.
split: 'c'.
split: 'd:e'.
END OF OUTPUT.
*/

int main(int /*argc*/, char ** /*argv*/)
{
	std::cout << std::format("strip: '{}'.\n",
			Webloop::strip(" \tA \tB \t\r\n"));
	std::cout << std::format("lstrip: '{}'.\n",
			Webloop::lstrip(" \tA \tB \t\r\n"));
	std::cout << std::format("rstrip: '{}'.\n",
			Webloop::rstrip(" \tA \tB \t\r\n"));
	std::cout << std::format("start: '{}'.\n",
			Webloop::startswith("abcd", "ab"));
	std::cout << std::format("start: '{}'.\n",
			Webloop::startswith("babcd", "ab"));
	std::cout << std::format("end: '{}'.\n",
			Webloop::endswith("abcd", "cd"));
	std::cout << std::format("end: '{}'.\n",
			Webloop::endswith("abcdc", "cd"));
	std::cout << std::format("upper: '{}'.\n",
			Webloop::upper("CamelCase"));
	std::cout << std::format("lower: '{}'.\n",
			Webloop::lower("CamelCase"));
	auto v = Webloop::split("one two\tthree\rfour\nfive ");
	for (size_t i = 0; i < v.size(); ++i)
		std::cout << std::format("split: '{}'.\n", v[i]);
	v = Webloop::split("a:b:c:d:e", 2, 2, ":");
	for (size_t i = 0; i < v.size(); ++i)
		std::cout << std::format("split: '{}'.\n", v[i]);
	return 0;
}

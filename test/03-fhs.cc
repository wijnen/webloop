#include <webloop/fhs.hh>
#include <iostream>

/*
OUTPUT:
int: 0.
END OF OUTPUT.
*/

int main(int argc, char **argv)
{
	Webloop::IntOption int_option("int-test", "test option (int)");
	Webloop::IntMultiOption int_multi_option("multi-int-test",
			"multi test option (int)");
	Webloop::fhs_init(argc, argv);
	std::cout << std::format("int: {}.\n", int_option.value);
	for (auto m: int_multi_option.value)
		std::cout << std::format("int: {}.\n", m);
	return 0;
}

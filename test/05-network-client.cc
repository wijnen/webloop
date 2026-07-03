#include <webloop/loop.hh>
#include <webloop/network.hh>
#include <iostream>

/*
OUTPUT:
int: 0.
END OF OUTPUT.
*/

int main(int argc, char **argv)
{
	auto loop = Webloop::Loop::get();
	loop->run();
	return 0;
}

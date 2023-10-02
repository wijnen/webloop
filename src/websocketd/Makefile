all: coroutine.elf

%.elf: %.o
	g++ -Wall -Wextra -std=c++23 -ggdb3 $^ -o $@

%.o: %.cc
	g++ -Wall -Wextra -std=c++23 -ggdb3 -c $< -o $@

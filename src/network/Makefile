all: test.elf test-server.elf

FLAGS = -std=c++23 -ggdb3 -Wall -Wextra

%.o: %.cc network.hh
	g++ $(FLAGS) -c $< -o $@

%.elf: %.o network.o
	g++ $(FLAGS) $^ -o $@

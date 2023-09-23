all: test.elf

%.o: %.cc fhs.hh
	g++ -std=c++23 -Wall -c $< -o $@

%.elf: %.o fhs.o
	g++ -std=c++23 -Wall $^ -o $@

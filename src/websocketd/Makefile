all: rpc-test.elf

FLAGS = -Wall -Wextra -I../network -I../fhs -std=c++23 -ggdb3

%.elf: %.o coroutine.o webobject.o ../network/network.o
	g++ ${FLAGS} $^ -o $@

%.o: %.cc coroutine.hh webobject.hh websocketd.hh
	g++ ${FLAGS} -c $< -o $@

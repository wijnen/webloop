TARGETS = test-httpd test-fhs test-network test-network-server test-rpc test-rpcd test-codes
PARTS = websocketd coroutine fhs network webobject url tools loop

HEADERS = $(addprefix ../include/webloop/,$(addsuffix .hh,${PARTS}))

all: $(addprefix build/,$(addsuffix .elf,${TARGETS}))

FLAGS = -std=c++23 -Wall -Wextra -ggdb3 -I../include

build/%.o: ../src/%.cc ${HEADERS}
	mkdir -p build
	g++ ${FLAGS} -c $< -o $@

build/%.o: %.cc ${HEADERS}
	mkdir -p build
	g++ ${FLAGS} -c $< -o $@

%.elf: %.o
	g++ ${FLAGS} $^ -o $@ -lwebloop

clean:
	rm -rf build

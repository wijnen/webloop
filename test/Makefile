TARGETS = test-httpd test-fhs test-network test-network-server test-rpc test-rpcd test-codes
PARTS = websocketd coroutine fhs network webobject url tools loop

HEADERS = $(addprefix ../include/webloop/,$(addsuffix .hh,${PARTS}))
OBJECTS = $(addprefix build/,$(addsuffix .o,${PARTS}))

all: $(addprefix build/,$(addsuffix .elf,${TARGETS}))

FLAGS = -std=c++23 -Wall -Wextra -ggdb3

build/%.o: ../src/%.cc ${HEADERS}
	mkdir -p build
	g++ ${FLAGS} -I../include/webloop -c $< -o $@

build/%.o: %.cc ${HEADERS}
	mkdir -p build
	g++ ${FLAGS} -I../include/webloop -c $< -o $@

%.elf: %.o ${OBJECTS}
	g++ ${FLAGS} $^ -o $@

clean:
	rm -rf build

.PRECIOUS: ${OBJECTS}

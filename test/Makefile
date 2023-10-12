TARGETS = test-httpd test-fhs test-network test-network-server test-rpc
PARTS = websocketd coroutine fhs network webobject url tools loop

HEADERS = $(addprefix ../include/,$(addsuffix .hh,${PARTS}))
OBJECTS = $(addprefix build/,$(addsuffix .o,${PARTS}))

all: $(addprefix build/,$(addsuffix .elf,${TARGETS}))

FLAGS = -std=c++23 -Wall -Wextra

build/%.o: ../src/%.cc ${HEADERS}
	mkdir -p build
	g++ ${FLAGS} -I../include -c $< -o $@

build/%.o: %.cc ${HEADERS}
	mkdir -p build
	g++ ${FLAGS} -I../include -c $< -o $@

%.elf: %.o ${OBJECTS}
	g++ ${FLAGS} $^ -o $@

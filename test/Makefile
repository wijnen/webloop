COMPONENTS = \
	  05-network-client \
	  06-network-server \
	  07-websocket-server \
	  08-websocket-client \
	  00-url \
	  01-tools \
	  02-loop \
	  03-fhs \
	  04-webobject

TARGETS = $(patsubst %,build/%.elf,${COMPONENTS})
TEST = $(patsubst %,test-%,${COMPONENTS})

all: ${TEST}

test-%: %.cc build/%.elf run-test Makefile
	./run-test $< $(filter %.elf,$^)

PARTS = websocketd fhs network webobject url tools loop

HEADERS = ../include/webloop.hh $(addprefix ../include/webloop/,$(addsuffix .hh,${PARTS}))

CPPFLAGS = -Wall -Wextra -I../include
CXXFLAGS = -std=c++26 -ggdb3
LDFLAGS = -std=c++23 -Wl,-rpath=/home/shevek/src/webloop/.libs
LIBADD = -L../.libs -lwebloop

build/%.o: %.cc ${HEADERS} Makefile
	mkdir -p build
	g++ ${CPPFLAGS} ${CXXFLAGS} -c $< -o $@

%.elf: %.o Makefile ../.libs/libwebloop.so
	g++ ${LDFLAGS} $< -o $@ ${LIBADD}

clean:
	rm -rf build

.PRECIOUS: ${TARGETS}

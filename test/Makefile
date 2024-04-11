TARGETS = 01-webobject httpd fhs network network-server rpc rpcd codes
PARTS = websocketd coroutine fhs network webobject url tools loop

HEADERS = ../include/webloop.hh $(addprefix ../include/webloop/,$(addsuffix .hh,${PARTS}))

test-%: TEST_RESULT = $(shell grep '^TEST RESULT: ' $(patsubst test-%,%,$@).cc | cut -f3- -d" ")
test-%: %.cc build/%.elf Makefile
	(grep '^TEST RESULT: ' $< || echo 'TEST RESULT: 0') | cut -f3- -d" " > build/expected-result-$@.txt
	grep '^TEST OUTPUT: ' $< | cut -f3- -d" " > build/expected-output-$@.txt
	grep '^TEST ERROR: ' $< | cut -f3- -d" " > build/expected-error-$@.txt
	echo 0 > build/result-$@.txt
	${filter %.elf,$^} > build/output-$@.txt 2> build/error-$@.txt || echo $$? > build/result-$@.txt
	diff -u build/expected-error-$@.txt build/error-$@.txt
	diff -u build/expected-result-$@.txt build/result-$@.txt
	diff -u build/expected-output-$@.txt build/output-$@.txt

all: $(addprefix test-,${TARGETS})

CPPFLAGS = -Wall -Wextra -I../include
CXXFLAGS = -std=c++23 -ggdb3
LDFLAGS = -std=c++23 -Wl,-rpath=/home/shevek/src/webloop/.libs
LIBADD = -L../.libs -lwebloop

build/%.o: %.cc ${HEADERS} Makefile ../.libs/libwebloop.so
	mkdir -p build
	g++ ${CPPFLAGS} ${CXXFLAGS} -c $< -o $@

%.elf: %.o Makefile
	g++ ${LDFLAGS} $< -o $@ ${LIBADD}

clean:
	rm -rf build

.PRECIOUS: build/01-webobject.elf

CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -D_POSIX_C_SOURCE=200809L
CFLAGS += -MMD -MP
ifeq ($(DEBUG),1)
CPPFLAGS += -DDEBUG
endif
ifeq ($(SANITIZE),1)
CFLAGS += -O1 -fno-omit-frame-pointer -fsanitize=address,undefined
LDFLAGS += -fsanitize=address,undefined
endif
OPENSSL_PREFIX ?= /opt/homebrew/opt/openssl@3
ifneq (,$(wildcard $(OPENSSL_PREFIX)/include/openssl/sha.h))
CPPFLAGS += -I$(OPENSSL_PREFIX)/include
LDFLAGS += -L$(OPENSSL_PREFIX)/lib
endif
LDLIBS += -lcrypto -pthread

COMMON = src/store.o src/ring.o src/args.o src/socket_layer.o
PROGRAMS = dkvs-client dkvs-server dkvs-dump-ring udp-test-client udp-test-server

.PHONY: all clean test check sanitize
all: $(PROGRAMS)

dkvs-client: src/client.o src/commands.o src/network.o $(COMMON)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

dkvs-server: src/server.o src/store.o src/args.o src/socket_layer.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

dkvs-dump-ring: src/dump.o $(COMMON)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

udp-test-client: src/udp-test-client.o src/args.o src/socket_layer.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

udp-test-server: src/udp-test-server.o src/args.o src/socket_layer.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

tests/test_unit: tests/test_unit.c $(COMMON)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc $(LDFLAGS) -o $@ $(filter %.c %.o,$^) $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

test: all tests/test_unit
	./tests/test_unit
	python3 -m unittest discover -s tests -v

check: test

sanitize:
	$(MAKE) clean
	$(MAKE) SANITIZE=1 test

clean:
	rm -f $(PROGRAMS) src/*.o src/*.d tests/test_unit tests/test_unit.d
	rm -rf tests/test_unit.dSYM

-include $(wildcard src/*.d) tests/test_unit.d

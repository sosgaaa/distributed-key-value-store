CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -D_POSIX_C_SOURCE=200809L
OPENSSL_PREFIX ?= /opt/homebrew/opt/openssl@3
ifneq (,$(wildcard $(OPENSSL_PREFIX)/include/openssl/sha.h))
CPPFLAGS += -I$(OPENSSL_PREFIX)/include
LDFLAGS += -L$(OPENSSL_PREFIX)/lib
endif
LDLIBS += -lcrypto -pthread

COMMON = src/store.o src/ring.o

.PHONY: all clean test
all: dkvs-client dkvs-server

dkvs-client: src/client.o $(COMMON)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

dkvs-server: src/server.o src/store.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

src/%.o: src/%.c src/dkvs.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

test: all
	python3 -m unittest discover -s tests -v

clean:
	rm -f dkvs-client dkvs-server src/*.o

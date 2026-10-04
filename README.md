# DKVS — distributed key-value store

C implementation of a distributed key-value store for EPFL CS-202, based on the [project specification](https://projprogsys-epfl.github.io/project/).

## Features

- UDP servers with an in-memory, mutex-protected hash table and one worker per request.
- SHA-1 consistent-hash ring with configurable virtual nodes and distinct physical replicas.
- Client `put` and `get` operations with configurable replication factor `N` and write/read quorums `W`/`R`.
- Timeouts, malformed-packet checks, length limits, and loopback integration tests.

The wire format follows the course specification: `key` for a read, `key\0value` for a write, a zero-byte success reply, and a one-byte `\0` missing-key reply. Values are limited to 512 bytes. Storage is in memory, so restarting a server clears its data. The client contacts replicas sequentially.

## Build and run

Requirements: a C11 compiler, pthreads, Python 3 for tests, and OpenSSL development headers (`libcrypto`).

```sh
make
make test
```

Create `servers.txt` with one line per physical server: `IP PORT VIRTUAL_NODES`.

```text
127.0.0.1 1234 2
127.0.0.1 1235 2
127.0.0.1 1236 2
```

Start one process per line, in separate terminals:

```sh
./dkvs-server 127.0.0.1 1234
./dkvs-server 127.0.0.1 1235
./dkvs-server 127.0.0.1 1236
```

Then run:

```sh
./dkvs-client ring
./dkvs-client put -n 3 -w 2 -- greeting hello
./dkvs-client get -n 3 -r 2 -- greeting
```

The default configuration path is `servers.txt`; use `-c PATH` to select another. The default `N` is the number of distinct servers, with `R=W=1`. A successful command exits with status 0; a failed quorum or missing key exits with status 1; invalid arguments exit with status 2.

## Specification

The implementation follows the EPFL CS-202 assignment handouts.

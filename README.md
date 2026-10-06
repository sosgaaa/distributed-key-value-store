# DKVS — Distributed Key-Value Store

**A small distributed storage system built in C.** Store a value under a key,
then retrieve it from a cluster of servers.

Developed in a team of two with **Ali El Azdi** for **Computer Systems
(CS-202)**, during my second year of Computer Science at **EPFL**.

**Stack:** C11 · UDP sockets · POSIX threads · OpenSSL · Python · Make

## What it does

- **Shares data across servers:** a hash ring decides where each key belongs.
- **Keeps multiple copies:** configurable read/write thresholds let operations
  succeed even when some servers do not respond.
- **Handles concurrent requests:** threads and mutexes protect shared data.
- **Provides a command-line client:** store, retrieve, concatenate, extract and
  search values, plus inspect the data held by each server.

For example, storing `greeting → hello` lets the client retrieve `hello`
through the cluster, without choosing a server manually.

## What I learned

This project connected data structures, networking and concurrency in one
working system. It gave me practical experience with memory management in C,
socket programming, synchronization, and debugging failures across processes.

Tests cover concurrent clients, unavailable servers, conflicting replies and
malformed packets. **27 Python tests and 4 C test groups pass**, including
checks with AddressSanitizer and UndefinedBehaviorSanitizer.

## Try it

Requires a C compiler, OpenSSL, Make and Python 3.

```sh
make
make test
```

See the [running guide](docs/USAGE.md) to start a local cluster, or explore the
[architecture](docs/ARCHITECTURE.md) for technical details. Data is stored in
memory and cleared when a server restarts.

The local hash function is adapted from
[idbenj's DKVS](https://github.com/idbenj/dkvs-distributed-key-value-store)
under the [MIT license](LICENSES/idbenj-MIT.txt).

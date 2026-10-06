# DKVS 📦

Put a value on one machine. Find it again through a few others.

This is my **Computer Systems (CS-202)** project from EPFL: a distributed key-value store written in **C**. The basic idea is a tiny `put` / `get` service, but with several servers sharing the work.

It connects a lot of course topics in one place: hash tables, sockets, threads, and what happens when a server stops replying. A command that looks simple in the terminal can involve several machines agreeing on an answer.

## What it does

- Stores string keys and values in memory.
- Talks to servers over **UDP**.
- Uses a **SHA-1 hash ring** to choose where keys belong.
- Supports virtual nodes and copies of a value on several servers.
- Lets you choose how many replies are needed for reads and writes.
- Handles requests with threads and protects the hash table with a mutex.

## Build

You need a **C11 compiler**, **Make**, **OpenSSL development headers**, and **Python 3** for the tests. On macOS with Homebrew OpenSSL, the Makefile checks `/opt/homebrew/opt/openssl@3`; use `OPENSSL_PREFIX` for another location.

```sh
make
make test
```

## Try a small cluster

Copy the example configuration:

```sh
cp servers.example.txt servers.txt
```

Each line contains an IP address, a port, and a number of virtual nodes. Start these servers in **three separate terminals**:

```sh
./dkvs-server 127.0.0.1 1234
./dkvs-server 127.0.0.1 1235
./dkvs-server 127.0.0.1 1236
```

Then, from a fourth terminal:

```sh
./dkvs-client ring
./dkvs-client put -n 3 -w 2 -- greeting hello
./dkvs-client get -n 3 -r 2 -- greeting
```

The last command should print `OK hello`.

Here, `-n 3` means three copies, `-w 2` means two successful write replies, and `-r 2` means two matching read replies. To try another configuration file, add `-c path/to/servers.txt`.

## A few details

Values are limited to **512 bytes**. Data lives in memory, so restarting a server clears it. The client contacts replicas one after another. The tests start real local UDP servers and check reads, updates, invalid input, and an unavailable server.

The course brief is available on the [CS-202 project website](https://projprogsys-epfl.github.io/project/).

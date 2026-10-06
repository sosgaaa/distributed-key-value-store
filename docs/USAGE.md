# Running and testing DKVS

## Build and verify

Requires a C11 compiler, Make, OpenSSL headers/libraries and Python 3.
On Debian/Ubuntu, install `build-essential libssl-dev python3`.
On macOS, install OpenSSL with Homebrew; the Makefile detects
`/opt/homebrew/opt/openssl@3`. Set `OPENSSL_PREFIX` for another installation.

```sh
make
make test
```

The tests run a real three-server loopback cluster and scripted UDP peers.
They cover failures, conflicting votes, duplicate and unknown senders,
malformed datagrams, empty values, maximum-size messages, multi-packet dumps,
concurrent clients and numeric overflow.

```sh
make sanitize              # rebuild and run with AddressSanitizer + UBSan
make clean
make DEBUG=1               # print network diagnostics to stderr
```

Use `make clean` when switching debug/sanitizer flags. `make check` aliases
`make test`. GitHub Actions runs the normal and sanitizer suites on Linux.

## Run a cluster

Copy the configuration:

```sh
cp servers.example.txt servers.txt
```

Each line contains `IP PORT VIRTUAL_NODE_COUNT`; comments begin with `#`.
Every physical IP/port pair appears once, with one or more virtual nodes.

Start the servers in **three separate terminals**:

```sh
./dkvs-server 127.0.0.1 1234
./dkvs-server 127.0.0.1 1235
./dkvs-server 127.0.0.1 1236
```

Servers may also receive initial key/value pairs:

```sh
./dkvs-server 127.0.0.1 1237 greeting hello empty ""
```

From another terminal:

```sh
./dkvs-client put -n 3 -w 2 -- greeting hello
./dkvs-client get -n 3 -r 2 -- greeting
```

Expected output: `OK`, then `OK hello`.

## Commands

```text
dkvs-client COMMAND [-c CONFIG] [-n N] [-r R] [-w W] [-t TIMEOUT_MS] -- ARGS
```

| Command | Arguments | Result |
| --- | --- | --- |
| `put` | `KEY VALUE` | Store or replace a value; print `OK`. |
| `get` | `KEY` | Print `OK VALUE` after R matching replies. |
| `cat` | `KEY... DEST` | Concatenate source values without separators and store at DEST. |
| `substr` | `KEY POSITION LENGTH DEST` | Extract a substring and store at DEST. Negative positions count from the end; zero length is allowed. |
| `find` | `HAYSTACK_KEY NEEDLE_KEY` | Print the first byte offset, or `OK -1` if the substring is absent. |
| `ring` | None | Print virtual nodes in SHA-1 order, including their digests. |
| `help` | None | Print command usage. |

Defaults: N is the number of physical servers, R=1, W=1, configuration
`servers.txt`, timeout 300 ms. Quorums must satisfy `1 <= R,W <= N <= S`.
The timeout applies separately to each GET/PUT in composite commands.

Use `-c path/to/servers.txt` for another configuration. Use `-t 1000` to allow
one second per operation. Exit codes are 0 for success, 1 for a failed
operation, and 2 for invalid input/configuration. Failed operations print
`FAIL`; a successful `find` that finds no substring prints `OK -1`.

For example:

```sh
./dkvs-client put -- first hello
./dkvs-client put -- second " world"
./dkvs-client cat -- first second joined
./dkvs-client get -- joined                    # OK hello world
./dkvs-client substr -- joined -5 5 tail
./dkvs-client get -- tail                      # OK world
./dkvs-client find -- joined tail              # OK 6
./dkvs-dump-ring
./dkvs-dump-ring -c servers.txt -t 1000
```

The dump executable lists the ring and asks each physical server for its data
once, even if it has several virtual nodes.

## Standalone UDP example

In one terminal:

```sh
./udp-test-server 127.0.0.1 1238
```

In another:

```sh
echo 213 | ./udp-test-client 127.0.0.1 1238      # 214
```

These examples exchange a 32-bit integer in network byte order. The server
replies with the next integer.

For implementation details and limitations, see [ARCHITECTURE.md](ARCHITECTURE.md).

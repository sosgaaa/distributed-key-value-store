# Architecture

## Modules and ownership

| Module | Responsibility |
| --- | --- |
| `args.c` | Strict numeric conversion and CLI option parsing. |
| `client.c` | Load a ring, validate quorums and dispatch a command. |
| `commands.c` | GET/PUT, concatenation, substring extraction and substring search. |
| `ring.c` | Parse static membership, hash virtual nodes, select distinct physical replicas. |
| `network.c` | Serialize requests, collect distinct votes, enforce operation deadlines. |
| `socket_layer.c` | IPv4 UDP sockets, endpoint comparison, nonblocking reception with `poll`. |
| `store.c` | Collision chains, owned strings, synchronized reads/writes and snapshots. |
| `server.c` | Validate datagrams and service requests in POSIX worker threads. |
| `dump.c` | List virtual nodes and request each physical server's contents once. |

Ring nodes hash the string `IP PORT VIRTUAL_ID`, with virtual IDs starting at 1.
Keys are hashed with SHA-1. Selection starts at the first node whose digest is
greater than or equal to the key digest, wraps around, and skips duplicate
physical endpoints until N servers have been selected. SHA-1 is used for
partition placement, not for authentication.

## Quorum exchange

Every GET or PUT opens a new socket, establishes an absolute deadline, sends
the request to all N selected servers, then reads responses. Each physical
endpoint can contribute at most once. A PUT needs W successful replies; a GET
needs R identical values. Missing-key replies are failures, not votes for an
empty value. Replies from outside the selected replica set are ignored.

The result buffer returned by `network_get()` belongs to its caller. Store
reads also return owned copies, so replacing a value cannot invalidate a value
already read by another thread. All store operations synchronize internally.

## Wire format

No final string terminator is sent unless the terminator itself is the
protocol's reply. The maximum IPv4 UDP payload is 65507 bytes; keys and values
are limited to 32753 bytes each.

| Operation | Request bytes | Successful response | Missing/error response |
| --- | --- | --- | --- |
| GET | `key` | `value`, including zero bytes for an empty value | One NUL byte |
| PUT | `key`, one NUL byte, `value` | One NUL byte | Zero bytes |
| DUMP | Zero bytes | Text snapshot in one or more datagrams | No snapshot on failure |

The old local implementation used the reverse PUT acknowledgement convention;
this implementation uses the course's convention. Both client and server need
to be rebuilt together.

## Dump and concurrency

The server captures `storing N key-value pairs:` and the formatted associations
in one consistent snapshot under its mutex. It then releases the mutex and
sends the snapshot in chunks of at most 60000 bytes. A long line may span
datagrams. `dkvs-dump-ring` writes received byte lengths directly, without
assuming packets are C strings, and contacts each physical server once.

Up to 128 worker threads can be active; when all are occupied, the main server
thread handles the next request synchronously. A 100 ms receive wait allows
SIGINT/SIGTERM to stop an idle server. Shutdown waits for existing workers
before freeing the table or closing its socket.

## Practical limits

Data is in memory and is lost on restart. Membership is static; changing the
ring does not migrate or repair existing values. There are no version vectors,
transactions, read repair or durable logs. R/W are response thresholds; they
do not imply linearizability under concurrent writes. Composite value commands
perform several independent operations and are not atomic.

UDP may lose or reorder packets. GET/PUT fail when the quorum is not met before
the deadline. A failed PUT may already have changed some replicas. Dump is a
diagnostic stream without sequence numbers or retransmission; its exit status
reports whether each server returned any data, not proof that every packet
arrived or that the whole cluster represents one instant. Source-address
validation is not cryptographic authentication.

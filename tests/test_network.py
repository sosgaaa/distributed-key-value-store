"""Scripted UDP peers exercise quorums independently of dkvs-server."""

import hashlib
import socket
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class QuorumTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.sockets = []
        self.threads = []
        self.stop = threading.Event()
        self.errors = []
        self.addCleanup(self.close_peers)
        for _ in range(3):
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 256 * 1024)
            sock.bind(("127.0.0.1", 0))
            sock.settimeout(0.05)
            self.sockets.append(sock)
        self.config = Path(self.temp.name) / "servers.txt"
        self.config.write_text("".join(
            f"127.0.0.1 {sock.getsockname()[1]} 3\n" for sock in self.sockets
        ), encoding="utf-8")

    def close_peers(self):
        self.stop.set()
        for sock in self.sockets:
            sock.close()
        for thread in self.threads:
            thread.join(timeout=2)

    def peers(self, handler):
        def serve(i, sock):
            while not self.stop.is_set():
                try:
                    request, peer = sock.recvfrom(65535)
                    handler(i, sock, request, peer)
                except socket.timeout:
                    continue
                except OSError as exc:
                    if not self.stop.is_set():
                        self.errors.append(exc)
                    break
                except Exception as exc:
                    self.errors.append(exc)
                    break
        for i, sock in enumerate(self.sockets):
            thread = threading.Thread(target=serve, args=(i, sock), daemon=True)
            thread.start()
            self.threads.append(thread)

    def client(self, command="get", *args):
        result = subprocess.run(
            [str(ROOT / "dkvs-client"), command, "-c", str(self.config), *args],
            capture_output=True, text=True, timeout=3, check=False,
        )
        self.assertEqual(self.errors, [])
        self.assertNotIn("Sanitizer", result.stderr)
        self.assertNotIn("runtime error:", result.stderr)
        return result

    def test_matching_votes_win_over_dissenting_replica(self):
        self.peers(lambda i, sock, request, peer: sock.sendto(b"A" if i < 2 else b"B", peer))
        self.assertEqual(self.client("get", "-r", "2", "--", "key").stdout, "OK A\n")

    def test_conflicting_votes_fail(self):
        self.peers(lambda i, sock, request, peer: sock.sendto(str(i).encode(), peer))
        result = self.client("get", "-r", "2", "--", "key")
        self.assertEqual((result.returncode, result.stdout), (1, "FAIL\n"))

    def test_duplicate_reads_from_one_server_cannot_reach_quorum(self):
        def handler(i, sock, request, peer):
            if i == 0:
                for _ in range(20):
                    sock.sendto(b"duplicate", peer)
        self.peers(handler)
        self.assertEqual(self.client("get", "-r", "2", "--", "key").stdout, "FAIL\n")

    def test_duplicate_write_acknowledgements_cannot_reach_quorum(self):
        def handler(i, sock, request, peer):
            if i == 0:
                for _ in range(20):
                    sock.sendto(b"\0", peer)
        self.peers(handler)
        self.assertEqual(self.client("put", "-w", "2", "--", "key", "value").stdout, "FAIL\n")

    def test_empty_write_response_is_failure(self):
        self.peers(lambda i, sock, request, peer: sock.sendto(b"", peer))
        self.assertEqual(self.client("put", "--", "key", "value").stdout, "FAIL\n")

    def test_unknown_sender_cannot_vote(self):
        def handler(i, sock, request, peer):
            if i == 0:
                sock.sendto(b"A", peer)
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as stranger:
                    for _ in range(10):
                        stranger.sendto(b"A", peer)
        self.peers(handler)
        self.assertEqual(self.client("get", "-r", "2", "--", "key").stdout, "FAIL\n")

    def test_unselected_known_server_cannot_vote(self):
        # Select successors independently, then cast a forged extra vote from
        # a server that belongs to the ring but is outside this operation's N.
        nodes = sorted((hashlib.sha1(
            f"127.0.0.1 {sock.getsockname()[1]} {vnode}".encode()
        ).digest(), i) for i, sock in enumerate(self.sockets) for vnode in range(1, 4))
        digest = hashlib.sha1(b"key").digest()
        start = next((i for i, (sha, _) in enumerate(nodes) if sha >= digest), 0)
        selected = []
        for offset in range(len(nodes)):
            index = nodes[(start + offset) % len(nodes)][1]
            if index not in selected:
                selected.append(index)
        voter, outsider = selected[0], selected[2]
        def handler(i, sock, request, peer):
            if i == voter:
                sock.sendto(b"A", peer)
                self.sockets[outsider].sendto(b"A", peer)
        self.peers(handler)
        self.assertEqual(self.client("get", "-n", "2", "-r", "2", "--", "key").stdout, "FAIL\n")

    def test_malformed_packet_does_not_block_valid_reply(self):
        def handler(i, sock, request, peer):
            sock.sendto(b"broken\0reply", peer)
            sock.sendto(b"ok", peer)
        self.peers(handler)
        self.assertEqual(self.client("get", "-r", "3", "--", "key").stdout, "OK ok\n")

    def test_oversized_reply_does_not_get_truncated_into_a_valid_value(self):
        def handler(i, sock, request, peer):
            sock.sendto(b"x" * 32754, peer)
            sock.sendto(b"ok", peer)
        self.peers(handler)
        self.assertEqual(self.client("get", "-r", "3", "--", "key").stdout, "OK ok\n")

    def test_single_byte_values_are_valid(self):
        self.peers(lambda i, sock, request, peer: sock.sendto(b"x", peer))
        self.assertEqual(self.client("get", "-r", "3", "--", "key").stdout, "OK x\n")

    def test_empty_values_count_as_votes(self):
        self.peers(lambda i, sock, request, peer: sock.sendto(b"", peer))
        self.assertEqual(self.client("get", "-r", "3", "--", "key").stdout, "OK \n")

    def test_missing_keys_are_not_empty_values(self):
        self.peers(lambda i, sock, request, peer: sock.sendto(b"\0", peer))
        self.assertEqual(self.client("get", "-r", "2", "--", "key").stdout, "FAIL\n")

    def test_all_requests_sent_before_waiting_for_any_response(self):
        contacted = [threading.Event() for _ in self.sockets]
        def handler(i, sock, request, peer):
            contacted[i].set()
            if all(event.wait(timeout=0.15) for event in contacted):
                sock.sendto(b"ok", peer)
        self.peers(handler)
        self.assertEqual(self.client("get", "-r", "3", "--", "key").stdout, "OK ok\n")

    def test_noise_cannot_extend_absolute_timeout(self):
        def handler(i, sock, request, peer):
            end = time.monotonic() + 0.7
            while time.monotonic() < end and not self.stop.is_set():
                sock.sendto(b"broken\0reply", peer)
                time.sleep(0.005)
        self.peers(handler)
        start = time.monotonic()
        result = self.client("get", "-r", "3", "-t", "200", "--", "key")
        elapsed = time.monotonic() - start
        self.assertEqual(result.stdout, "FAIL\n")
        self.assertLess(elapsed, 0.6)


if __name__ == "__main__":
    unittest.main()

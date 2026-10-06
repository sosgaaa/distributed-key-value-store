"""End-to-end checks against real loopback UDP servers."""

import socket
import subprocess
import tempfile
import time
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAX_ELEMENT = 32753


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class DistributedStoreTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.ports = [free_port() for _ in range(3)]
        self.config = Path(self.temp.name) / "servers.txt"
        self.config.write_text(
            "".join(f"127.0.0.1 {port} 2\n" for port in self.ports),
            encoding="utf-8",
        )
        self.servers = [
            subprocess.Popen(
                [str(ROOT / "dkvs-server"), "127.0.0.1", str(port)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
            )
            for port in self.ports
        ]
        self.addCleanup(self.stop_servers)
        time.sleep(0.1)
        for server in self.servers:
            self.assertIsNone(server.poll())

    def stop_servers(self):
        for server in self.servers:
            if server.poll() is None:
                server.terminate()
        for server in self.servers:
            try:
                _, error = server.communicate(timeout=2)
            except subprocess.TimeoutExpired:
                server.kill()
                server.communicate()
                self.fail("Server did not stop cleanly")
            self.assertEqual(server.returncode, 0, error.decode(errors="replace"))
            self.assertNotIn(b"Sanitizer", error)
            self.assertNotIn(b"runtime error:", error)

    def client(self, *args):
        result = subprocess.run(
            [str(ROOT / "dkvs-client"), args[0], "-c", str(self.config), *args[1:]],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )
        self.assertNotIn("Sanitizer", result.stderr)
        self.assertNotIn("runtime error:", result.stderr)
        return result

    def test_put_get_update_and_empty_value(self):
        self.assertEqual(self.client("put", "-n", "3", "-w", "2", "--", "alpha", "one").stdout, "OK\n")
        self.assertEqual(self.client("get", "-n", "3", "-r", "2", "--", "alpha").stdout, "OK one\n")
        self.assertEqual(self.client("put", "-w", "2", "--", "alpha", "two").stdout, "OK\n")
        self.assertEqual(self.client("get", "-r", "2", "--", "alpha").stdout, "OK two\n")
        self.assertEqual(self.client("put", "-w", "2", "--", "empty", "").stdout, "OK\n")
        self.assertEqual(self.client("get", "-r", "2", "--", "empty").stdout, "OK \n")
        missing = self.client("get", "--", "missing")
        self.assertEqual((missing.returncode, missing.stdout), (1, "FAIL\n"))

    def test_quorum_with_unavailable_server(self):
        self.servers[0].terminate()
        self.servers[0].wait(timeout=2)
        self.assertEqual(self.client("put", "-n", "3", "-w", "2", "--", "key", "value").stdout, "OK\n")
        self.assertEqual(self.client("get", "-n", "3", "-r", "2", "--", "key").stdout, "OK value\n")
        failed = self.client("put", "-n", "3", "-w", "3", "--", "other", "value")
        self.assertEqual((failed.returncode, failed.stdout), (1, "FAIL\n"))

    def test_ring_has_unique_physical_servers(self):
        result = self.client("ring")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(len(result.stdout.splitlines()), 6)
        self.assertEqual(len({line.split()[0] for line in result.stdout.splitlines()}), 3)

    def test_invalid_arguments_and_config(self):
        self.assertEqual(self.client("put", "-w", "4", "--", "a", "b").returncode, 2)
        self.assertEqual(self.client("get", "--", "").returncode, 2)
        self.config.write_text("127.0.0.1 99999 2\n", encoding="utf-8")
        self.assertEqual(self.client("ring").returncode, 2)

    def test_value_commands(self):
        self.assertEqual(self.client("put", "--", "a", "hello").stdout, "OK\n")
        self.assertEqual(self.client("put", "--", "b", " world").stdout, "OK\n")
        self.assertEqual(self.client("cat", "--", "a", "b", "joined").stdout, "OK\n")
        self.assertEqual(self.client("get", "--", "joined").stdout, "OK hello world\n")
        self.assertEqual(self.client("substr", "--", "joined", "-5", "5", "tail").stdout, "OK\n")
        self.assertEqual(self.client("get", "--", "tail").stdout, "OK world\n")
        self.assertEqual(self.client("find", "--", "joined", "tail").stdout, "OK 6\n")
        self.assertEqual(self.client("find", "--", "a", "tail").stdout, "OK -1\n")
        self.assertEqual(self.client("substr", "--", "joined", "3", "0", "empty").stdout, "OK\n")
        self.assertEqual(self.client("find", "--", "joined", "empty").stdout, "OK 0\n")
        self.assertEqual(self.client("substr", "--", "joined", "-99", "2", "bad").stdout, "FAIL\n")
        self.assertEqual(self.client("substr", "--", "joined", "0", "12", "bad").stdout, "FAIL\n")
        self.assertEqual(self.client("substr", "--", "joined", "-9223372036854775808", "0", "bad").stdout, "FAIL\n")
        self.assertEqual(self.client("substr", "--", "joined", "junk", "1", "bad").returncode, 2)
        self.assertEqual(self.client("substr", "--", "joined", "0", "-1", "bad").returncode, 2)

    def test_failed_cat_preserves_destination(self):
        self.client("put", "--", "a", "x" * 17000)
        self.client("put", "--", "b", "y" * 17000)
        self.client("put", "--", "destination", "original")
        self.assertEqual(self.client("cat", "--", "a", "b", "destination").stdout, "FAIL\n")
        self.assertEqual(self.client("get", "--", "destination").stdout, "OK original\n")
        self.assertEqual(self.client("cat", "--", "a", "missing", "destination").stdout, "FAIL\n")
        self.assertEqual(self.client("get", "--", "destination").stdout, "OK original\n")

    def test_maximum_size_and_wire_protocol(self):
        key = "k" * MAX_ELEMENT
        value = "v" * MAX_ELEMENT
        self.assertEqual(self.client("put", "-w", "3", "--", key, value).stdout, "OK\n")
        self.assertEqual(self.client("get", "-r", "3", "--", key).stdout, f"OK {value}\n")
        self.assertEqual(self.client("put", "--", "too-big", value + "x").returncode, 2)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.settimeout(1)
            peer = ("127.0.0.1", self.ports[0])
            sock.sendto(b"raw\0value", peer)
            self.assertEqual(sock.recv(65535), b"\0")
            sock.sendto(b"raw", peer)
            self.assertEqual(sock.recv(65535), b"value")
            sock.sendto(b"raw\0", peer)
            self.assertEqual(sock.recv(65535), b"\0")
            sock.sendto(b"raw", peer)
            self.assertEqual(sock.recv(65535), b"")
            sock.sendto(b"bad\0value\0extra", peer)
            self.assertEqual(sock.recv(65535), b"")
            sock.sendto(b"bad", peer)
            self.assertEqual(sock.recv(65535), b"\0")

    def test_weak_write_still_sends_to_all_replicas(self):
        self.assertEqual(self.client("put", "-w", "1", "--", "replicated", "yes").stdout, "OK\n")
        for port in self.ports:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.settimeout(1)
                sock.sendto(b"replicated", ("127.0.0.1", port))
                self.assertEqual(sock.recv(65535), b"yes")

    def test_concurrent_clients(self):
        def write_and_read(i):
            key, value = f"parallel-{i}", f"value-{i}"
            write = self.client("put", "-w", "3", "--", key, value)
            read = self.client("get", "-r", "3", "--", key)
            return write.stdout, read.stdout, value
        with ThreadPoolExecutor(max_workers=8) as pool:
            for write, read, value in pool.map(write_and_read, range(32)):
                self.assertEqual(write, "OK\n")
                self.assertEqual(read, f"OK {value}\n")

    def test_dump_multiple_packets_and_virtual_node_deduplication(self):
        # A single maximum-size pair is larger than a dump datagram once formatted.
        key, value = "k" * MAX_ELEMENT, "v" * MAX_ELEMENT
        self.assertEqual(self.client("put", "-w", "3", "--", key, value).stdout, "OK\n")
        result = subprocess.run(
            [str(ROOT / "dkvs-dump-ring"), "-c", str(self.config)],
            capture_output=True, text=True, timeout=5, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.count("storing 1 key-value pairs:"), 3)
        self.assertEqual(result.stdout.count(f"{key} --> {value}\n"), 3)

    def test_empty_dump_and_unavailable_server(self):
        empty = subprocess.run([str(ROOT / "dkvs-dump-ring"), "-c", str(self.config)],
                               capture_output=True, text=True, timeout=5, check=False)
        self.assertEqual(empty.stdout.count("storing 0 key-value pairs:"), 3)
        self.assertEqual(empty.returncode, 0)
        self.servers[0].terminate()
        self.servers[0].wait(timeout=2)
        partial = subprocess.run([str(ROOT / "dkvs-dump-ring"), "-c", str(self.config)],
                                 capture_output=True, text=True, timeout=5, check=False)
        self.assertEqual(partial.returncode, 1)
        self.assertIn("No dump reply", partial.stderr)
        self.assertEqual(partial.stdout.count("storing 0 key-value pairs:"), 2)

    def test_strict_numeric_and_config_validation(self):
        for count in ("-1", "+1", "0", "1junk", "18446744073709551616"):
            self.assertEqual(self.client("get", "-n", count, "--", "a").returncode, 2)
        for config in ("127.0.0.1 1234 -1\n", "127.0.0.1 +1234 2\n",
                       "127.0.0.1 1234 1025\n", "bad-ip 1234 2\n",
                       "127.0.0.1 1234 1\n127.0.0.1 1234 2\n", ""):
            self.config.write_text(config, encoding="utf-8")
            self.assertEqual(self.client("ring").returncode, 2)

    def test_udp_example(self):
        port = free_port()
        server = subprocess.Popen([str(ROOT / "udp-test-server"), "127.0.0.1", str(port)])
        try:
            time.sleep(0.05)
            result = subprocess.run([str(ROOT / "udp-test-client"), "127.0.0.1", str(port)],
                                    input="213\n", capture_output=True, text=True, timeout=2)
            self.assertEqual((result.returncode, result.stdout), (0, "214\n"))
        finally:
            server.terminate()
            server.wait(timeout=2)


if __name__ == "__main__":
    unittest.main()

"""End-to-end checks against real loopback UDP servers."""

import socket
import subprocess
import tempfile
import time
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


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
                server.communicate(timeout=2)
            except subprocess.TimeoutExpired:
                server.kill()
                server.communicate()

    def client(self, *args):
        return subprocess.run(
            [str(ROOT / "dkvs-client"), args[0], "-c", str(self.config), *args[1:]],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )

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


if __name__ == "__main__":
    unittest.main()

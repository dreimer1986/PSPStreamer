import socket
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class RemoteHttpTests(unittest.TestCase):
    def test_watchdog_storage_failures_readiness_and_both_stall_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "watch"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "psp-client"), str(ROOT / "tests/watchdog_harness.c"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=3)

    def test_real_socket_fragmentation_eof_timeout_and_cancel(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "remote"
            subprocess.run(["cc", "-D_POSIX_C_SOURCE=200809L", "-std=c11", "-Wall", "-Wextra",
                            "-Werror", "-I", str(ROOT / "psp-client"),
                            str(ROOT / "tests/remote_http_harness.c"), "-o", str(binary)], check=True)
            for mode in ("ok", "short", "oversize", "timeout", "cancel"):
                with self.subTest(mode=mode), socket.socket() as listener:
                    listener.bind(("127.0.0.1", 0))
                    listener.listen()
                    listener.settimeout(3)
                    def serve():
                        with listener.accept()[0] as client:
                            client.settimeout(3)
                            client.recv(4096)
                            if mode == "cancel":
                                return
                            if mode == "timeout":
                                time.sleep(2.1)
                                return
                            packet = b"HTTP/1.0 200 OK\r\nContent-Length: "
                            packet += b"9000\r\n\r\n{}" if mode == "oversize" else b"2\r\n\r\n{}"
                            if mode == "short":
                                packet = packet[:-1]
                            try:
                                for byte in packet:
                                    client.sendall(bytes([byte]))
                                    time.sleep(.001)
                            except (BrokenPipeError, ConnectionResetError):
                                pass
                    thread = threading.Thread(target=serve)
                    thread.start()
                    try:
                        subprocess.run([str(binary), str(listener.getsockname()[1]), mode], check=True, timeout=4)
                    finally:
                        thread.join(timeout=4)
                    self.assertFalse(thread.is_alive())

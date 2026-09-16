"""Optional native TLS integration: point MBEDTLS_HOST_SOURCE/BUILD at 2.28."""
import os
from pathlib import Path
import socket
import ssl
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TlsTransportTests(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("MBEDTLS_HOST_SOURCE") and os.environ.get("MBEDTLS_HOST_BUILD"),
                         "Set MBEDTLS_HOST_SOURCE/BUILD for native TLS tests")
    def test_real_handshakes_rotation_buffered_reads_timeout_and_cancel(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            binary = base / "tls"
            subprocess.run(["cc", "-D_POSIX_C_SOURCE=200809L", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=undefined", "-I", str(ROOT / "tests/tls_host"), "-I", str(ROOT / "psp-client"),
                "-I", str(Path(os.environ["MBEDTLS_HOST_SOURCE"]) / "include"),
                str(ROOT / "tests/tls_transport_harness.c"), str(ROOT / "psp-client/tls_transport.c"),
                "-L", str(Path(os.environ["MBEDTLS_HOST_BUILD"]) / "library"),
                "-lmbedtls", "-lmbedx509", "-lmbedcrypto", "-pthread", "-o", str(binary)], check=True)
            contexts = []
            for index in range(2):
                cert, key = base / f"cert{index}.pem", base / f"key{index}.pem"
                subprocess.run(["openssl", "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1",
                    "-nodes", "-keyout", str(key), "-out", str(cert), "-days", "1", "-subj", "/CN=localhost"],
                    check=True, capture_output=True)
                context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); context.load_cert_chain(cert, key)
                contexts.append(context)
            for mode in ("rotate", "storage", "cancel", "timeout"):
                work = base / mode; work.mkdir()
                if mode == "storage": (work / "certificates").touch()
                with self.subTest(mode=mode), socket.socket() as listener:
                    listener.bind(("127.0.0.1", 0)); listener.listen(); listener.settimeout(5)
                    failures = []
                    def serve():
                        try:
                            for i in range(3 if mode == "rotate" else 1):
                                with listener.accept()[0] as raw:
                                    raw.settimeout(3)
                                    if mode in ("cancel", "timeout"):
                                        time.sleep(.3); continue
                                    with contexts[i == 2].wrap_socket(raw, server_side=True) as client:
                                        data = b""
                                        while b"\r\n\r\n" not in data: data += client.recv(4096)
                                        client.sendall(b'HTTP/1.0 200 OK\r\nContent-Length: 11\r\n\r\n{"ok":true}')
                        except Exception as error: failures.append(error)
                    thread = threading.Thread(target=serve); thread.start()
                    try:
                        subprocess.run([str(binary), str(listener.getsockname()[1]), mode], cwd=work, check=True, timeout=10)
                    finally: thread.join(6)
                    self.assertFalse(thread.is_alive()); self.assertEqual(failures, [])
                if mode == "rotate":
                    self.assertEqual(len(list((work / "certificates").glob("*.der"))), 1)
                    self.assertEqual(len(list((work / "certificates").glob("*.bak"))), 1)

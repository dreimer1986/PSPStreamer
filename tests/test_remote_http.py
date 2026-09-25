import socket
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class RemoteHttpTests(unittest.TestCase):
    def test_directory_budget_includes_dns_and_classifies_permanent_errors(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / 'library-fetch'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'psp-client'), str(ROOT / 'tests/library_fetch_harness.c'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=3)

    def test_media_preparation_stays_responsive_and_can_cancel_then_retry(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / 'media-request'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pthread',
                            '-I', str(ROOT / 'psp-client'), str(ROOT / 'tests/media_request_harness.c'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=4)

    def test_subtitles_finish_before_video_framebuffer_switch(self):
        source = (ROOT / 'psp-client/main.c').read_text()
        start = source.index('result=prepare_client_subtitles(media_id,subtitle_tv_profile);')
        switch = source.index('tvout_video_active = tvout_begin_video() == 0;', start)
        self.assertIn('return result==MEDIA_REQUEST_CANCELLED?0:result;', source[start:switch])
        bitmap = source.index('if(tv_profile && !offline_active)return 0;')
        self.assertLess(bitmap, source.index('"/api/bitmap-subtitles/', bitmap))
        self.assertIn('if (result < 0) return offline_active ? 0 : result;', source)

    def test_idle_browser_poll_is_asynchronous_and_cancellable(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "browser-remote"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pthread",
                            "-I", str(ROOT / "psp-client"), str(ROOT / "tests/browser_remote_harness.c"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=3)

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
            for mode in ("ok", "lowercase", "short", "oversize", "timeout", "cancel", "unauthorized", "post"):
                with self.subTest(mode=mode), socket.socket() as listener:
                    listener.bind(("127.0.0.1", 0))
                    listener.listen()
                    listener.settimeout(3)
                    def serve():
                        with listener.accept()[0] as client:
                            client.settimeout(3)
                            request=client.recv(4096)
                            if mode=='post':
                                while b'\r\n\r\n' not in request:request+=client.recv(4096)
                                header,body=request.split(b'\r\n\r\n',1)
                                while len(body)<30000:body+=client.recv(4096)
                                self.assertTrue(header.startswith(b'POST /api/comfort/sync HTTP/1.0'))
                                self.assertIn(b'Content-Length: 30000',header)
                                self.assertEqual(body,b'x'*30000)
                            if mode == "cancel":
                                return
                            if mode == "timeout":
                                time.sleep(2.1)
                                return
                            packet = b"HTTP/1.0 200 OK\r\nContent-Length: "
                            packet += b"9000\r\n\r\n{}" if mode == "oversize" else b"2\r\n\r\n{}"
                            if mode == "lowercase":
                                packet=packet.replace(b"Content-Length:",b"content-length:")
                            if mode == "unauthorized":
                                packet=packet.replace(b"200 OK",b"401 Unauthorized")
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

import socket
import ssl
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from psp_streamer.stream_pause import StreamPauses, write_stream


class StreamPauseTests(unittest.TestCase):
    def test_lease_identity_expiry_resume_and_cleanup(self):
        pauses = StreamPauses()
        lease = pauses.begin('psp', 'episode')
        other = pauses.begin('other', 'episode')
        with patch('psp_streamer.stream_pause.time.monotonic', return_value=100):
            pauses.report('psp', 'wrong', True)
            self.assertFalse(pauses.paused(lease))
            pauses.report('psp', 'episode', True)
            self.assertTrue(pauses.paused(lease))
            self.assertFalse(pauses.paused(other))
        with patch('psp_streamer.stream_pause.time.monotonic', return_value=144):
            self.assertTrue(pauses.paused(lease))
        with patch('psp_streamer.stream_pause.time.monotonic', return_value=145):
            self.assertFalse(pauses.paused(lease))
            pauses.report('psp', 'episode', True)
            pauses.report('psp', 'episode', False)
            self.assertFalse(pauses.paused(lease))
        pauses.end(lease)
        pauses.end(other)
        self.assertEqual(pauses.active, {})

    def test_partial_send_timeout_never_repeats_bytes(self):
        class Connection:
            output = bytearray()
            calls = 0
            def send(self, data):
                self.calls += 1
                if self.calls == 2:
                    raise TimeoutError()
                n = min(3, len(data))
                self.output.extend(data[:n])
                return n
        connection = Connection()
        write_stream(connection, b'FLV-packet-boundaries', 180, lambda: False)
        self.assertEqual(connection.output, b'FLV-packet-boundaries')

    def test_paused_socket_survives_then_resumes_without_corruption(self):
        sender, receiver = socket.socketpair()
        sender.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 4096)
        sender.settimeout(.01)
        receiver.settimeout(2)
        payload = bytes(range(256)) * 4096
        paused = threading.Event()
        paused.set()
        errors = []
        def run():
            try:
                write_stream(sender, payload, .05, paused.is_set)
            except Exception as error:
                errors.append(error)
        worker = threading.Thread(target=run)
        try:
            worker.start()
            time.sleep(.15)  # Three times the ordinary inactivity limit.
            self.assertTrue(worker.is_alive())
            paused.clear()
            received = bytearray()
            while len(received) < len(payload):
                received.extend(receiver.recv(65536))
            worker.join(2)
            self.assertFalse(worker.is_alive())
            self.assertEqual(errors, [])
            self.assertEqual(received, payload)
        finally:
            sender.close()
            receiver.close()
            worker.join(2)

    def test_unpaused_stalled_client_still_times_out(self):
        sender, receiver = socket.socketpair()
        try:
            sender.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 4096)
            sender.settimeout(.01)
            with self.assertRaises(TimeoutError):
                write_stream(sender, b'x' * 1000000, .04, lambda: False)
        finally:
            sender.close()
            receiver.close()

    def test_https_pause_preserves_encrypted_stream(self):
        with tempfile.TemporaryDirectory() as directory:
            cert, key = Path(directory)/'cert.pem', Path(directory)/'key.pem'
            subprocess.run(['openssl', 'req', '-x509', '-newkey', 'ec', '-pkeyopt',
                'ec_paramgen_curve:prime256v1', '-nodes', '-keyout', str(key),
                '-out', str(cert), '-days', '1', '-subj', '/CN=localhost'],
                check=True, capture_output=True)
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(cert, key)
            client = ssl.create_default_context(cafile=str(cert))
            sender, raw_receiver = socket.socketpair()
            sender.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 4096)
            sender.settimeout(2)
            raw_receiver.settimeout(2)
            payload = bytes(range(256))*4096
            paused = threading.Event()
            paused.set()
            errors = []
            def run():
                try:
                    with context.wrap_socket(sender, server_side=True) as encrypted:
                        encrypted.settimeout(.01)
                        write_stream(encrypted, payload, .05, paused.is_set)
                except Exception as error:
                    errors.append(error)
            worker = threading.Thread(target=run)
            worker.start()
            try:
                with client.wrap_socket(raw_receiver, server_hostname='localhost') as receiver:
                    time.sleep(.15)
                    self.assertTrue(worker.is_alive())
                    paused.clear()
                    received = bytearray()
                    while len(received) < len(payload):
                        chunk = receiver.recv(65536)
                        if not chunk:
                            break
                        received.extend(chunk)
                    self.assertEqual(received, payload)
                worker.join(2)
                self.assertFalse(worker.is_alive())
                self.assertEqual(errors, [])
            finally:
                sender.close()
                raw_receiver.close()
                worker.join(2)

import unittest
import http.client
import os
import sys
import tempfile
import threading
from pathlib import Path
from unittest.mock import patch
from psp_streamer.stream_trace import StreamTrace
from psp_streamer.stream_pause import write_stream


class StreamTraceTests(unittest.TestCase):
    def test_http_transcode_preserves_bytes_and_releases_slot(self):
        from psp_streamer.server import AppServer, Library, MediaItem
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ, {
            'PSP_STREAMER_SETTINGS_DIR': directory, 'SERVER_PASSWORD': ''
        }):
            source = Path(directory)/'sample.mkv'
            source.touch()
            library = Library([Path(directory)])
            token = library.encode(MediaItem(0, 'sample.mkv'))
            payload = bytes(range(256))*100
            command = [sys.executable, '-c', 'import sys;sys.stdout.buffer.write(bytes(range(256))*100)']
            with patch('psp_streamer.server.ffmpeg_command', return_value=command), \
                 patch('http.server.BaseHTTPRequestHandler.log_message') as log, \
                 AppServer(('127.0.0.1', 0), library) as server:
                worker = threading.Thread(target=server.serve_forever)
                worker.start()
                connection = http.client.HTTPConnection(*server.server_address, timeout=3)
                try:
                    connection.request('GET', f'/api/transcode/{token}?container=flv')
                    response = connection.getresponse()
                    self.assertEqual(response.status, 200)
                    self.assertEqual(response.read(), payload)
                    phases = [call.args[2] for call in log.call_args_list
                              if call.args and call.args[0].startswith('stream transport')]
                    self.assertIn('begin', phases)
                    self.assertIn('ffmpeg exit 0', phases)
                    self.assertEqual(server.stream_pauses.active, {})
                finally:
                    connection.close()
                    server.shutdown()
                    worker.join(3)

    def test_progress_and_rate_limit(self):
        records = []
        with patch('psp_streamer.stream_trace.time.monotonic', return_value=100) as clock:
            trace = StreamTrace(17, lambda fmt, *args: records.append(fmt % args))
            trace.report('begin', True)
            trace.received(10)
            trace.delivered(4)
            self.assertEqual(len(records), 1)
            clock.return_value = 131
            trace.delivered(0)
            self.assertIn('phase=client backpressure', records[-1])
            self.assertIn('read=10 sent=4 read_age=31.0 send_age=31.0', records[-1])
            clock.return_value = 162
            trace.report('waiting for ffmpeg')
            self.assertIn('waiting for ffmpeg', records[-1])
            trace.report('end', True)
            self.assertEqual(len(records), 4)

    def test_partial_send_reports_exact_bytes(self):
        counts = []
        class Connection:
            calls = 0
            output = bytearray()
            def send(self, data):
                self.calls += 1
                if self.calls == 2:
                    raise TimeoutError()
                self.output.extend(data[:2])
                return min(2, len(data))
        connection = Connection()
        write_stream(connection, b'1234567', 180, lambda: False, counts.append)
        self.assertEqual(connection.output, b'1234567')
        self.assertEqual(counts, [2, 0, 2, 2, 1])

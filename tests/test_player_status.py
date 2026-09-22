import http.client
import json
import tempfile
import threading
import unittest
from pathlib import Path
from unittest.mock import patch

from psp_streamer.player_status import PlayerStatus
from psp_streamer.server import AppServer, Library


class PlayerStatusTests(unittest.TestCase):
    def test_confirmation_expiry_and_persistent_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            status = PlayerStatus(directory)
            self.assertEqual(status.identity, PlayerStatus(directory).identity)
            self.assertFalse(status.snapshot()['online'])
            with patch('psp_streamer.player_status.time.monotonic', return_value=100):
                status.remember('file', {'name': 'Episode'}, 'video')
                status.report({'media': ['file'], 'state': ['playing'], 'started': ['0']})
                self.assertEqual(status.snapshot()['state'], 'buffering')
                status.report({'media': ['file'], 'state': ['playing'], 'started': ['1'], 'position': ['12000']})
                self.assertEqual(status.snapshot()['position'], 12)
                self.assertEqual(status.snapshot()['title'], 'Episode')
            with patch('psp_streamer.player_status.time.monotonic', return_value=146):
                self.assertFalse(status.snapshot()['online'])
                status.report({'media': ['file'], 'state': ['paused']})
                self.assertEqual(status.snapshot()['state'], 'paused')
                status.report({'media': ['file'], 'state': ['stopped']})
                self.assertEqual(status.snapshot()['state'], 'idle')
                self.assertNotIn('id', status.snapshot())

    def test_read_only_api_cannot_keep_psp_online_or_consume_command(self):
        with tempfile.TemporaryDirectory() as directory, patch.dict('os.environ',
                {'PSP_STREAMER_SETTINGS_DIR': directory, 'PSP_STREAMER_DOWNLOAD_DIR': directory+'/downloads',
                 'PSP_STREAMER_PASSWORD': ''}):
            server = AppServer(('127.0.0.1', 0), Library([Path(directory)]))
            thread = threading.Thread(target=server.serve_forever)
            thread.start()
            connection = http.client.HTTPConnection(*server.server_address, timeout=5)
            try:
                queued = server.set_remote_command({'action': 'pause'})
                for _ in range(3):
                    connection.request('GET', '/api/player')
                    reply = connection.getresponse()
                    self.assertEqual(reply.status, 200)
                    self.assertFalse(json.loads(reply.read())['online'])
                self.assertEqual(server.remote_after(0), queued)
                connection.request('GET', '/api/remote/next?after=0')
                reply = connection.getresponse()
                self.assertEqual(json.loads(reply.read())['action'], 'pause')
                self.assertTrue(server.player_status.snapshot()['online'])
                self.assertEqual(server.player_status.snapshot()['state'], 'idle')
                server.set_remote_command({'action': 'play', 'id': 'unconfirmed'})
                self.assertEqual(server.player_status.snapshot()['state'], 'idle')
            finally:
                connection.close()
                server.shutdown()
                thread.join()
                server.server_close()

import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from psp_streamer.server import AppServer, Library
from psp_streamer.state_storage import state_directory


class StateStorageTests(unittest.TestCase):
    def test_restart_preserves_sources_identity_and_preferences(self):
        with tempfile.TemporaryDirectory() as temp, patch.dict(os.environ, {
                'PSP_STREAMER_STATE_DIR': temp, 'PSP_STREAMER_SETTINGS_DIR': '',
                'PSP_STREAMER_LEGACY_STATE_DIR': '', 'PSP_STREAMER_RADIO_DIR': '',
                'PSP_STREAMER_DOWNLOAD_DIR': '', 'PSP_STREAMER_TLS_CERT': '',
                'PSP_STREAMER_TLS_KEY': '', 'PSP_STREAMER_PASSWORD': 'ha-managed'}):
            with AppServer(('127.0.0.1', 0), Library([Path(temp)])) as server:
                server.plex.config.update(token='test-plex', enabled=True, files=False)
                server.plex.save()
                server.jellyfin.config.update(token='test-jellyfin', enabled=True)
                server.jellyfin.save()
                server.radio.change({'name': 'Test station', 'url': 'https://example.org/live.mp3'})
                preferences = {'audio_quality': 'v5', 'audio': 'jpn', 'subtitle': 'deu'}
                server.offline.preferences(preferences)
                identity = server.player_status.identity
                self.assertEqual(server.radio.path.parent, Path(temp))
                self.assertEqual(server.offline.root, Path(temp) / 'downloads')
                self.assertIsNone(server.settings.path)  # HA retains password control.
            with AppServer(('127.0.0.1', 0), Library([Path(temp)])) as restored:
                self.assertEqual(restored.plex.config['token'], 'test-plex')
                self.assertFalse(restored.plex.config['files'])
                self.assertEqual(restored.jellyfin.config['token'], 'test-jellyfin')
                self.assertTrue(restored.jellyfin.config['enabled'])
                self.assertEqual(restored.player_status.identity, identity)
                self.assertEqual(restored.radio.list()[0]['name'], 'Test station')
                self.assertEqual(restored.offline.preferences(), preferences)
                self.assertTrue(restored.settings.verify(b'ha-managed'))

    def test_legacy_copy_never_overwrites_or_deletes(self):
        with tempfile.TemporaryDirectory() as temp:
            old, new = Path(temp) / 'old', Path(temp) / 'new'
            old.mkdir(); new.mkdir()
            (old / 'plex.json').write_text('{"token":"legacy"}')
            (old / 'jellyfin.json').write_text('{"token":"obsolete"}')
            (new / 'jellyfin.json').write_text('{"token":"current"}')
            with patch.dict(os.environ, {'PSP_STREAMER_SETTINGS_DIR': '',
                    'PSP_STREAMER_STATE_DIR': str(new),
                    'PSP_STREAMER_LEGACY_STATE_DIR': str(old)}):
                self.assertEqual(state_directory(), new)
                state_directory()
            self.assertEqual((new / 'plex.json').read_bytes(), (old / 'plex.json').read_bytes())
            self.assertEqual((new / 'plex.json').stat().st_mode & 0o777, 0o600)
            self.assertIn('current', (new / 'jellyfin.json').read_text())
            self.assertEqual(list(new.glob('.migrate-*')), [])

    def test_docker_settings_override_state_directory(self):
        with tempfile.TemporaryDirectory() as temp, patch.dict(os.environ, {
                'PSP_STREAMER_SETTINGS_DIR': temp, 'PSP_STREAMER_STATE_DIR': '/unused',
                'PSP_STREAMER_LEGACY_STATE_DIR': ''}):
            self.assertEqual(state_directory(), Path(temp))

    def test_ha_launcher_selects_persistent_storage(self):
        root = Path(__file__).resolve().parents[1]
        launcher = (root / 'psp_streamer_addon/run.sh').read_text()
        self.assertIn('export PSP_STREAMER_STATE_DIR=/data', launcher)
        self.assertNotIn('export PSP_STREAMER_SETTINGS_DIR=', launcher)

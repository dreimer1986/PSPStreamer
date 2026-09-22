"""Run with a dedicated venv containing Home Assistant, not the server venv.

python -m unittest tests.ha_integration_checks -v
"""
import json
import tempfile
import threading
import unittest
from pathlib import Path
from unittest.mock import AsyncMock, Mock, patch

import aiohttp
from homeassistant.core import HomeAssistant
from homeassistant.data_entry_flow import AbortFlow
from homeassistant.components.media_player import MediaPlayerState

from custom_components.psp_streamer.api import Api, AuthError, normalize_url
from custom_components.psp_streamer.config_flow import ConfigFlow
from custom_components.psp_streamer.coordinator import PlayerCoordinator
from custom_components.psp_streamer.media_player import Player
from custom_components.psp_streamer.const import FOLDER, DOMAIN
from psp_streamer.server import AppServer, Library


class IntegrationTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.env = patch.dict('os.environ', {'PSP_STREAMER_SETTINGS_DIR': self.temp.name,
            'PSP_STREAMER_DOWNLOAD_DIR': self.temp.name+'/downloads', 'PSP_STREAMER_PASSWORD': 'test-ä'})
        self.env.start()
        self.addCleanup(self.env.stop)
        media = Path(self.temp.name)/'media'
        media.mkdir()
        (media/'Track.mp3').touch()
        self.server = AppServer(('127.0.0.1', 0), Library([media]))
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.start()
        self.session = aiohttp.ClientSession()
        self.url = f'http://127.0.0.1:{self.server.server_port}'
        self.api = Api(self.session, self.url, 'test-ä')
        self.hass = HomeAssistant(self.temp.name)

    async def asyncTearDown(self):
        await self.session.close()
        import asyncio
        await asyncio.to_thread(self.server.shutdown)
        self.thread.join()
        self.server.server_close()

    async def test_authenticated_status_and_no_optimistic_state(self):
        with self.assertRaises(AuthError):
            await Api(self.session, self.url, 'wrong').status()
        coordinator = PlayerCoordinator(self.hass, None, self.api)
        coordinator.data = await coordinator._async_update_data()
        coordinator.last_update_success = True
        player = Player(coordinator)
        self.assertFalse(player.available)
        self.server.player_status.report({'state': ['playing'], 'media': ['episode'],
                                         'position': ['123000'], 'duration': ['300000']})
        coordinator.data = await coordinator._async_update_data()
        self.assertTrue(player.available)
        self.assertEqual(player.state, MediaPlayerState.PLAYING)
        self.assertEqual(player.media_position, 123)
        coordinator.async_request_refresh = AsyncMock()
        await player.async_media_pause()
        self.assertEqual(self.server.remote_after(0)['action'], 'pause')
        self.assertEqual(player.state, MediaPlayerState.PLAYING)
        self.server.player_status.report({'media': ['episode'], 'state': ['paused']})
        coordinator.data = await coordinator._async_update_data()
        self.assertEqual(player.state, MediaPlayerState.PAUSED)
        await player.async_media_seek(32)
        self.assertEqual(self.server.remote_after(0)['seconds'], 32)
        await player.async_media_stop()
        self.assertEqual(self.server.remote_after(0)['action'], 'stop')

    async def test_real_browse_and_play_requests(self):
        self.server.player_status.report({})
        coordinator = PlayerCoordinator(self.hass, None, self.api)
        coordinator.data = await coordinator._async_update_data()
        coordinator.last_update_success = True
        coordinator.async_request_refresh = AsyncMock()
        player = Player(coordinator)
        root = await player.async_browse_media()
        self.assertIn('Files', [c.title for c in root.children])
        folder = await player.async_browse_media(FOLDER, json.dumps([0, ':files:/0']))
        song = folder.children[0]
        await player.async_play_media(DOMAIN, song.media_content_id, extra={'audio_quality': 'v5'})
        command = self.server.remote_after(0)
        self.assertEqual(command['kind'], 'audio')
        self.assertEqual(command['audio_quality'], 'v5')

    async def test_config_flow_connect_and_auth_failure(self):
        flow = ConfigFlow()
        flow.hass = self.hass
        flow.context = {'source': 'user'}
        flow.flow_id = 'test'
        flow.async_set_unique_id = AsyncMock()
        flow._abort_if_unique_id_configured = Mock()
        with patch('custom_components.psp_streamer.config_flow.async_get_clientsession', return_value=self.session):
            result = await flow.async_step_user({'url': self.url, 'password': 'wrong'})
            self.assertEqual(result['errors']['base'], 'invalid_auth')
            result = await flow.async_step_user({'url': self.url+'/', 'password': 'test-ä'})
            self.assertEqual(result['type'], 'create_entry')
            self.assertEqual(result['data']['url'], self.url)
            self.assertEqual(flow.async_set_unique_id.call_args.args[0], self.server.player_status.identity)

    def test_packaging_and_url_validation(self):
        root = Path(__file__).resolve().parents[1]
        for path in ['hacs.json', 'custom_components/psp_streamer/manifest.json',
                     'custom_components/psp_streamer/strings.json',
                     'custom_components/psp_streamer/translations/de.json']:
            json.loads((root/path).read_text())
        self.assertTrue((root/'custom_components/psp_streamer/brand/icon.png').is_file())
        for url in ['ftp://example.org', 'https://user:password@example.org', 'http://host:99999', 'https://host/?secret=x']:
            with self.assertRaises(ValueError):
                normalize_url(url)

    async def test_reconfigure_and_reauth_check_server_identity(self):
        for step, source in [('reconfigure', 'reconfigure'), ('reauth_confirm', 'reauth')]:
            flow = ConfigFlow()
            flow.hass = self.hass
            flow.context = {'source': source}
            flow.flow_id = 'test'
            entry = Mock(data={'url': self.url}, unique_id=self.server.player_status.identity)
            flow._get_reauth_entry = Mock(return_value=entry)
            flow._get_reconfigure_entry = Mock(return_value=entry)
            async def set_identity(identity):
                flow.context['unique_id'] = identity
            flow.async_set_unique_id = set_identity
            flow.async_update_reload_and_abort = Mock(return_value={'type': 'abort'})
            with patch('custom_components.psp_streamer.config_flow.async_get_clientsession', return_value=self.session):
                await flow._form(step, {'url': self.url, 'password': 'test-ä'})
                flow.async_update_reload_and_abort.assert_called_once_with(entry,
                    data_updates={'url': self.url, 'password': 'test-ä'})
                entry.unique_id = 'another-server'
                with self.assertRaises(AbortFlow):
                    await flow._form(step, {'url': self.url, 'password': 'test-ä'})

    async def test_setup_and_unload_forward_media_player(self):
        from custom_components.psp_streamer import async_setup_entry, async_unload_entry
        from homeassistant.config_entries import ConfigEntries
        self.hass.config_entries = ConfigEntries(self.hass, {})
        entry = Mock(data={'url': self.url, 'password': 'test-ä'})
        coordinator = Mock(async_config_entry_first_refresh=AsyncMock())
        with patch('custom_components.psp_streamer.async_get_clientsession', return_value=self.session), \
             patch('custom_components.psp_streamer.PlayerCoordinator', return_value=coordinator), \
             patch.object(self.hass.config_entries, 'async_forward_entry_setups', new=AsyncMock()) as setup, \
             patch.object(self.hass.config_entries, 'async_unload_platforms', new=AsyncMock(return_value=True)) as unload:
            self.assertTrue(await async_setup_entry(self.hass, entry))
            coordinator.async_config_entry_first_refresh.assert_awaited_once()
            self.assertIs(entry.runtime_data, coordinator)
            setup.assert_awaited_once()
            self.assertTrue(await async_unload_entry(self.hass, entry))
            unload.assert_awaited_once()

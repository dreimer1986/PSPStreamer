import json
import http.client
import os
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import patch

from psp_streamer.plex import Plex
from psp_streamer.server import Library, MediaItem, AppServer


class PlexTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.media = self.root / 'media'
        self.media.mkdir()
        (self.media / 'episode.mkv').touch()
        self.plex = Plex(self.root / 'settings', [self.media])
        self.addCleanup(self.plex.close)
        self.plex.config.update(enabled=True, token='secret-server', account='secret-account',
                                url='http://plex:32400', mappings=[{'plex': '/plex/tv', 'local': str(self.media)}])
        self.row = {'ratingKey': '42', 'type': 'episode', 'title': 'Übung',
                    'grandparentTitle': 'Show', 'parentTitle': 'Season 1', 'parentIndex': 1, 'index': 2,
                    'duration': 240000, 'viewOffset': 45000,
                    'Media': [{'Part': [{'file': '/plex/tv/episode.mkv'}]}]}

    def test_mapped_original_and_private_persistence(self):
        with patch.object(self.plex, 'request', return_value={'MediaContainer': {'Metadata': [self.row]}}):
            self.assertEqual(self.plex.source(self.plex.token('42')), self.media / 'episode.mkv')
            details = self.plex.details(self.plex.token('42'))
            self.assertEqual(details['resume'], 45)
            self.assertIn('S01E02', details['name'])
        self.plex.save()
        self.assertEqual(self.plex.path.stat().st_mode & 0o777, 0o600)
        self.assertNotIn('secret', json.dumps(self.plex.public()))
        restored = Plex(self.root / 'settings', [self.media])
        self.assertEqual(restored.config['client'], self.plex.config['client'])
        self.assertEqual(restored.config['token'], 'secret-server')

    def test_unsafe_local_paths_never_open_outside_roots(self):
        from psp_streamer.plex_media import RemoteSource
        (self.root / 'outside.mkv').touch()
        (self.media / 'link.mkv').symlink_to(self.root / 'outside.mkv')
        self.row['Media'][0]['Part'][0]['key'] = '/library/parts/42/file.mkv'
        self.row['Media'][0]['Part'][0]['file'] = '/plex/tv/../outside.mkv'
        with patch.object(self.plex, 'metadata', return_value=self.row), self.assertRaises(ValueError):
            self.plex.source(self.plex.token('42'))
        for source in ('/plex/tv/link.mkv', '/other/file.mkv', '/plex/tv-other/episode.mkv'):
            self.row['Media'][0]['Part'][0]['file'] = source
            with patch.object(self.plex, 'metadata', return_value=self.row):
                self.assertIsInstance(self.plex.source(self.plex.token('42')), RemoteSource)

    def test_multipart_rejected_explicitly(self):
        self.row['Media'][0]['Part'].append({'file': '/plex/tv/second.mkv'})
        with patch.object(self.plex, 'metadata', return_value=self.row), self.assertRaisesRegex(ValueError, 'multipart'):
            self.plex.source(self.plex.token('42'))

    def test_sources_validate_without_destroying_settings(self):
        self.plex.configure(dict(enabled=True, files=False, radio=False, mappings=self.plex.config['mappings']))
        before = dict(self.plex.config)
        with self.assertRaises(ValueError):
            self.plex.configure(dict(enabled=False, files=False, radio=False, mappings=[]))
        self.assertEqual(self.plex.config, before)
        with self.assertRaises(ValueError):
            self.plex.configure(dict(enabled=True, files=True, radio=False,
                mappings=[{'plex': '/plex', 'local': str(self.root)}]))

    def test_disabled_files_do_not_disable_plex_originals(self):
        library = Library([self.media])
        library.plex = self.plex
        self.plex.config['files'] = False
        with patch.object(self.plex, 'metadata', return_value=self.row):
            self.assertEqual(library.decode(self.plex.token('42'))[1], self.media / 'episode.mkv')
        with self.assertRaisesRegex(ValueError, 'disabled'):
            library.decode(library.encode(MediaItem(0, 'episode.mkv')))

    def test_pin_link_and_poll_never_return_credentials(self):
        with patch.object(self.plex, 'request', side_effect=[{'id': 7, 'code': 'ab+c'}, {'authToken': 'new-secret'}]):
            link = self.plex.link()
            self.assertTrue(link['url'].startswith('https://app.plex.tv/auth#?'))
            self.assertIn('ab%2Bc', link['url'])
            self.assertEqual(self.plex.poll(), {'linked': True})
            self.assertEqual(self.plex.config['account'], 'new-secret')
        with self.assertRaisesRegex(ValueError, 'expired'):
            self.plex.poll()

    def test_only_discovered_identity_checked_connections(self):
        self.plex.config['enabled'] = False
        before = dict(self.plex.config)
        self.plex.resources = [{'clientIdentifier': 'server1', 'accessToken': 'resource-secret',
                                'connections': [{'uri': 'https://host.plex.direct:32400'}]}]
        with self.assertRaises(ValueError):
            self.plex.select('server1', 'https://attacker.invalid')
        with patch.object(self.plex, 'request', return_value={'MediaContainer': {'machineIdentifier': 'other'}}):
            with self.assertRaises(ValueError):
                self.plex.select('server1', 'https://host.plex.direct:32400')
        self.assertEqual(self.plex.config, before)
        with patch.object(self.plex, 'request', side_effect=ValueError('Unavailable')):
            with self.assertRaises(ValueError):
                self.plex.select('server1', 'https://host.plex.direct:32400')
        self.assertEqual(self.plex.config, before)
        with patch.object(self.plex, 'request', return_value={'MediaContainer': {'machineIdentifier': 'server1'}}):
            selected = self.plex.select('server1', 'https://host.plex.direct:32400')
        self.assertNotIn('resource-secret', json.dumps(selected))
        self.assertTrue(selected['enabled'])
        for key in ('files', 'radio', 'mappings'):
            self.assertEqual(self.plex.config[key], before[key])
        reopened = Plex(self.root / 'settings', [self.media])
        self.addCleanup(reopened.close)
        self.assertTrue(reopened.config['enabled'])
        self.plex.configure(dict(enabled=False, files=True, radio=True,
                                 mappings=self.plex.config['mappings']))
        self.assertFalse(self.plex.public()['enabled'])

    def test_pagination_and_playlist_context(self):
        with patch.object(self.plex, 'listing', return_value={'Metadata': [self.row], 'totalSize': 102}):
            page = self.plex.browse(0, ':plex:p8@100')
            self.assertEqual(page['videos'][0]['id'], self.plex.token('42.p8.100'))
            self.assertEqual(page['folders'][-1]['path'], ':plex:p8@101')
            self.assertEqual(page['videos'][0]['name'], 'S01E02 Übung')
        following = dict(self.row, ratingKey='43')
        with patch.object(self.plex, 'listing', side_effect=[{'Metadata': [self.row]}, {'Metadata': [following]}]):
            self.assertEqual(self.plex.next_media(self.plex.token('42.p8.100'))['id'], self.plex.token('43.p8.101'))
        with patch.object(self.plex, 'listing', return_value={'Metadata': [following]}):
            self.assertEqual(self.plex.next_media(self.plex.token('42.p8.100')), {})

    def test_invalid_ids_rejected(self):
        for token in ('plex.x', 'plex.42/evil', 'plex.42.p1.-1', 'plex.' + '9' * 100):
            with self.assertRaises(ValueError):
                self.plex.split(token)

    def test_ids_are_bound_to_the_selected_server(self):
        token = self.plex.token('42.p8.1')
        self.assertEqual(self.plex.split(token), ('42', 'p', '8', '1'))
        self.plex.config['server'] = 'different-server'
        with self.assertRaisesRegex(ValueError, 'identifier'):
            self.plex.split(token)

    def test_timeline_is_async_throttled_and_reports_measured_ms(self):
        event = threading.Event()
        def request(path):
            event.set()
            return {}
        with patch.object(self.plex, 'request', side_effect=request) as call:
            self.plex.report(self.plex.token('42'), 'playing', 17001, 240000)
            self.assertTrue(event.wait(2))
            self.plex.report(self.plex.token('42'), 'playing', 17002, 240000)
            self.assertEqual(call.call_count, 1)
            self.assertIn('time=17001', call.call_args.args[0])
            event.clear()
            self.plex.report(self.plex.token('42'), 'stopped', 18000, 240000)
            self.assertTrue(event.wait(2))
            self.assertIn('state=stopped', call.call_args.args[0])
        self.assertEqual(self.plex.report_error, '')

    def test_server_attaches_provider_and_closes_worker(self):
        with patch.dict(os.environ, {'PSP_STREAMER_SETTINGS_DIR': str(self.root / 'server')}):
            with AppServer(('127.0.0.1', 0), Library([self.media])) as server:
                self.assertIs(server.library.plex, server.plex)
                self.assertFalse(server.plex.public()['enabled'])

    def test_http_browse_metadata_remote_and_disabled_sources(self):
        with patch.dict(os.environ, {'PSP_STREAMER_SETTINGS_DIR': str(self.root / 'http'),
                                     'PSP_STREAMER_PASSWORD': ''}):
            with AppServer(('127.0.0.1', 0), Library([self.media])) as server:
                server.plex.config.update(self.plex.config)
                token = server.plex.token('42.s1.0')
                worker = threading.Thread(target=server.serve_forever)
                worker.start()
                def request(method, path, data=None):
                    client = http.client.HTTPConnection(*server.server_address, timeout=3)
                    try:
                        client.request(method, path, json.dumps(data) if data is not None else None,
                                       {'Content-Type': 'application/json'})
                        response = client.getresponse()
                        return response.status, json.loads(response.read())
                    finally:
                        client.close()
                try:
                    with patch.object(server.plex, 'request', return_value={'MediaContainer': {'Metadata': [self.row]}}):
                        status, listing = request('GET', '/api/library?path=:plex:s1')
                        self.assertEqual(status, 200)
                        self.assertEqual(listing['videos'][0]['id'], token)
                        probe = type('Probe', (), {'returncode': 0, 'stdout': json.dumps({
                            'format': {'duration': '240'}, 'streams': [{'codec_type': 'audio',
                                'tags': {'language': 'ger'}}, {'codec_type': 'subtitle', 'tags': {'language': 'ger'}}]})})()
                        with patch('psp_streamer.server.subprocess.run', return_value=probe):
                            status, metadata = request('GET', '/api/metadata/' + token)
                        self.assertEqual(status, 200)
                        self.assertEqual(metadata['s'][0]['l'], 'ger')
                        self.assertEqual(metadata['resume'], 45)
                        status, command = request('POST', '/api/remote/command', {'action': 'play', 'id': token, 'subtitle': 0})
                        self.assertEqual(status, 200)
                        self.assertEqual(command['subtitle'], 0)
                        self.assertEqual(command['kind'], 'video')
                        status, _ = request('POST', '/api/plex/settings', dict(enabled=True, files=False,
                            radio=False, mappings=server.plex.config['mappings']))
                        self.assertEqual(status, 200)
                        status, listing = request('GET', '/api/library')
                        self.assertEqual(status, 200)
                        self.assertEqual(listing['folders'], [{'name': 'Plex', 'path': ':plex:'}])
                        self.assertFalse(listing['videos'])
                finally:
                    server.shutdown()
                    worker.join()

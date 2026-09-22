"""Artwork is optional, authenticated and independent of playback."""
import http.client
import json
from pathlib import Path
import tempfile
import subprocess
import threading
import unittest
from email.message import Message
from unittest.mock import MagicMock, patch

from psp_streamer.plex import Plex
from psp_streamer.jellyfin import Jellyfin
from psp_streamer.server import AppServer, Library


class ArtworkTests(unittest.TestCase):
    def test_psp_menu_bounds_and_request_lifetime(self):
        root=Path(__file__).resolve().parents[1]
        binary=Path(self.temp.name)/'menu-artwork'
        subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror',
            '-I',str(root/'psp-client'),str(root/'tests/menu_artwork_harness.c'),
            '-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True,timeout=5)

    def test_psp_packet_conversion_cache_and_series_parent(self):
        import struct
        token=self.plex.token('42');parent=self.plex.token('1')
        row={'ratingKey':'1','thumb':'/library/metadata/1/thumb/1','art':'/library/metadata/1/art/1'}
        logo=(Path(__file__).resolve().parents[1]/'static/logo.png').read_bytes()
        with patch.object(self.plex,'metadata',side_effect=[{'type':'episode','grandparentRatingKey':'1'},row]) as metadata, \
                patch.object(self.plex.artwork,'get',return_value=(logo,'image/png')) as get:
            packet=self.plex.artwork.psp(token)
            self.assertEqual(struct.unpack('<4sHHHHII',packet[:20]),(b'PSPA',320,180,80,112,115200,17920))
            self.assertEqual(len(packet),133140)
            self.assertEqual(metadata.call_args.args[0],parent)
            self.assertEqual(get.call_count,2)
            self.assertEqual(self.plex.artwork.psp(token),packet)
            self.assertEqual(get.call_count,2)

    def test_psp_missing_art_is_small_and_disabled_source_cannot_use_cache(self):
        token=self.jf.token('a'*32)
        with patch.object(self.jf,'metadata',return_value={'Id':'a'*32}):
            self.assertEqual(len(self.jf.artwork.psp(token)),20)
            self.jf.config['enabled']=False
            with self.assertRaises(ValueError):self.jf.artwork.psp(token)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.plex = Plex(self.temp.name, [])
        self.jf = Jellyfin(self.temp.name, [])
        for p in (self.plex, self.jf):
            p.config.update(enabled=True, token='never-public', url='http://provider.invalid', user='a'*32)
            self.addCleanup(p.close)

    def response(self, mime='image/jpeg', data=b'jpeg-test'):
        response = MagicMock()
        response.headers = Message(); response.headers['Content-Type'] = mime
        response.read.return_value = data
        response.__enter__.return_value = response
        return response

    def test_provider_fields_and_unsafe_paths(self):
        a = self.plex.artwork
        token = self.plex.token('42')
        links = a.links(dict(thumb='/library/metadata/42/thumb/123', art='/library/metadata/1/art/456'), token)
        self.assertEqual(set(links), {'cover', 'backdrop'})
        self.assertNotIn('never-public', json.dumps(links))
        for path in ('https://evil.invalid/image', '//evil/image', '/:/prefs', '/library/metadata/42/thumb/../../prefs'):
            self.assertEqual(a.links(dict(thumb=path), token), {})
        paths = self.jf.artwork.paths(dict(Id='a'*32, ImageTags=dict(Primary='cover'),
            ParentBackdropItemId='b'*32, ParentBackdropImageTags=['background']))
        self.assertIn('/Items/'+'a'*32+'/Images/Primary?', paths['cover'])
        self.assertIn('/Items/'+'b'*32+'/Images/Backdrop?', paths['backdrop'])
        self.assertEqual(self.jf.artwork.paths(dict(Id='../../evil', ImageTags=dict(Primary='tag')))['cover'], '')

    def test_lazy_image_cache_credentials_and_revocation(self):
        token = self.plex.token('42'); a = self.plex.artwork
        a.links(dict(thumb='/library/metadata/42/thumb/123'), token)
        with patch('psp_streamer.artwork.build_opener') as opener, patch.object(self.plex, 'metadata') as metadata:
            opener.return_value.open.return_value = self.response()
            self.assertEqual(a.get(token, 'cover'), (b'jpeg-test', 'image/jpeg'))
            self.assertEqual(a.get(token, 'cover'), (b'jpeg-test', 'image/jpeg'))
            self.assertEqual(opener.return_value.open.call_count, 1)
            metadata.assert_not_called()  # No N+1 metadata requests for catalogue thumbnails.
            request = opener.return_value.open.call_args.args[0]
            self.assertEqual(request.get_header('X-plex-token'), 'never-public')
            self.assertTrue(request.full_url.startswith('http://provider.invalid/photo/:/transcode?'))
            self.plex.config['enabled'] = False
            with self.assertRaises(ValueError): a.get(token, 'cover')

    def test_reject_html_and_oversized_images(self):
        token = self.jf.token('a'*32); a = self.jf.artwork
        a.links(dict(Id='a'*32, ImageTags=dict(Primary='tag')), token)
        for mime, data in [('text/html', b'<script>'), ('image/jpeg', b'x'*(a.max_image+1))]:
            with patch('psp_streamer.artwork.build_opener') as opener:
                opener.return_value.open.return_value = self.response(mime, data)
                with self.assertRaises(ValueError): a.get(token, 'cover')
        self.assertEqual(a.cache_bytes, 0)

    def test_authenticated_http_and_current_media_art(self):
        with patch.dict('os.environ', {'PSP_STREAMER_SETTINGS_DIR': self.temp.name,
                'PSP_STREAMER_DOWNLOAD_DIR': self.temp.name+'/downloads','PSP_STREAMER_PASSWORD': 'test'}):
            server = AppServer(('127.0.0.1', 0), Library([Path(self.temp.name)]))
        thread = threading.Thread(target=server.serve_forever); thread.start()
        connection = http.client.HTTPConnection(*server.server_address, timeout=5)
        try:
            path = '/api/artwork/plex.42.0123456789ab/cover'
            connection.request('GET', path)
            reply = connection.getresponse(); reply.read(); self.assertEqual(reply.status, 401)
            connection.request('GET', '/api/psp-artwork?item=:plex:m42')
            reply = connection.getresponse(); reply.read(); self.assertEqual(reply.status, 401)
            with patch.object(server.plex.artwork, 'psp', return_value=b'PSPA-packet') as packet:
                connection.request('GET', '/api/psp-artwork?item=:plex:m42', headers={'Authorization': 'Basic cHNwOnRlc3Q='})
                reply=connection.getresponse(); self.assertEqual(reply.status,200)
                self.assertEqual(reply.read(),b'PSPA-packet')
                packet.assert_called_once_with(server.plex.token('42'))
            with patch.object(server.plex.artwork, 'get', return_value=(b'JPEG', 'image/jpeg')):
                connection.request('GET', path, headers={'Authorization': 'Basic cHNwOnRlc3Q='})
                reply = connection.getresponse(); self.assertEqual(reply.read(), b'JPEG')
                self.assertEqual(reply.getheader('Cache-Control'), 'private, no-store')
            server.player_status.remember('episode', {'name':'Episode', 'artwork':{'cover':path}}, 'video')
            server.player_status.report({'media':['episode'], 'state':['playing']})
            self.assertEqual(server.player_status.snapshot()['artwork']['cover'], path)
            listing = {'root':0,'path':'','parent':None,'folders':[],
                       'videos':[{'id':'episode','name':'Episode','artwork':{'cover':path}}]}
            with patch('psp_streamer.server.browse_catalogue', return_value=listing):
                for web in (False,True):
                    headers={'Authorization':'Basic cHNwOnRlc3Q='}
                    if web: headers['X-PSP-Web']='1'
                    connection.request('GET','/api/library',headers=headers)
                    reply=connection.getresponse(); payload=json.loads(reply.read())
                    self.assertEqual('artwork' in payload['videos'][0],web)
                self.assertIn('artwork',listing['videos'][0])
        finally:
            connection.close(); server.shutdown(); thread.join(); server.server_close()

    def test_theme_is_user_scoped_and_never_a_playback_report(self):
        with patch.object(self.jf, 'request', return_value={'Items':[{'Id':'b'*32}]}) as request, \
                patch('psp_streamer.artwork.build_opener') as opener:
            opener.return_value.open.return_value = self.response('audio/mpeg', b'MP3')
            self.assertEqual(self.jf.artwork.theme(self.jf.token('a'*32)), (b'MP3','audio/mpeg'))
            self.assertIn('inheritFromParent=true', request.call_args.args[0])
            self.assertIn('userId='+'a'*32, request.call_args.args[0])
            self.assertIn('/Audio/'+'b'*32+'/stream?static=true', opener.return_value.open.call_args.args[0].full_url)

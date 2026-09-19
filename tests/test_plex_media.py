"""Real HTTP byte ranges and FFmpeg against an unmounted Plex original."""
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import struct
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from psp_streamer.plex import Plex
from psp_streamer.plex_media import RemoteSource
from psp_streamer.server import AppServer, Library, ffmpeg_command, load_roots


class OriginalHandler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_HEAD(self):
        self.do_GET(head=True)

    def do_GET(self, head=False):
        self.server.seen.append((self.path, dict(self.headers)))
        if self.headers.get('X-Plex-Token') != 'upstream-secret':
            self.send_error(401)
            return
        if self.server.redirect:
            self.send_response(302)
            self.send_header('Location', self.server.redirect)
            self.end_headers()
            return
        data = self.server.media
        start, end = 0, len(data)-1
        wanted = self.headers.get('Range')
        if wanted:
            first, last = wanted[6:].split('-')
            start = int(first) if first else max(0, len(data)-int(last))
            end = min(int(last), end) if last and first else end
            if start >= len(data):
                self.send_error(416)
                return
        self.send_response(206 if wanted else 200)
        self.send_header('Content-Length', str(end-start+1))
        self.send_header('Accept-Ranges', 'bytes')
        if wanted:
            self.send_header('Content-Range', f'bytes {start}-{end}/{len(data)}')
        self.end_headers()
        if not head:
            try:
                self.wfile.write(data[start:end+1])
            except (BrokenPipeError, ConnectionResetError):
                pass


class PlexMediaTests(unittest.TestCase):
    def test_plex_only_server_needs_no_filesystem_root(self):
        self.assertEqual(load_roots(''), [])
        self.assertEqual(Library([]).browse(0)['videos'], [])
        with self.assertRaises(ValueError):
            Library([]).browse(0, '/etc')

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.upstream = ThreadingHTTPServer(('127.0.0.1', 0), OriginalHandler)
        self.upstream.media = bytes(range(256))*16
        self.upstream.redirect = ''
        self.upstream.seen = []
        worker = threading.Thread(target=self.upstream.serve_forever, daemon=True)
        worker.start()
        self.addCleanup(self.upstream.server_close)
        self.addCleanup(self.upstream.shutdown)
        self.plex = Plex(self.root, [])
        self.addCleanup(self.plex.close)
        self.plex.config.update(enabled=True, token='upstream-secret', server='fake-plex',
            url=f'http://127.0.0.1:{self.upstream.server_port}')
        self.row = {'ratingKey': '7', 'type': 'episode', 'title': 'Episode', 'updatedAt': 123,
            'Media': [{'container': 'mkv', 'Part': [{'file': '/unmounted/Übung.mkv',
                'key': '/library/parts/91/123/file.mkv', 'size': len(self.upstream.media)}]}]}
        mocked = patch.object(self.plex, 'metadata', return_value=self.row)
        mocked.start()
        self.addCleanup(mocked.stop)

    def test_original_ranges_head_credentials_and_server_change(self):
        source = self.plex.source(self.plex.token('7'))
        self.assertIsInstance(source, RemoteSource)
        self.assertEqual(source.name, 'Übung.mkv')
        self.assertNotIn('upstream-secret', str(source))
        with urlopen(Request(str(source), headers={'Range': 'bytes=100-199'})) as reply:
            self.assertEqual(reply.status, 206)
            self.assertEqual(reply.headers['Content-Range'], 'bytes 100-199/4096')
            self.assertEqual(reply.read(), self.upstream.media[100:200])
        with urlopen(Request(str(source), method='HEAD')) as reply:
            self.assertEqual(reply.headers['Content-Length'], '4096')
            self.assertEqual(reply.read(), b'')
        self.assertEqual(self.upstream.seen[0][1]['X-Plex-Token'], 'upstream-secret')
        self.assertEqual(self.upstream.seen[0][0], '/library/parts/91/123/file.mkv')
        with self.assertRaises(HTTPError) as error:
            urlopen(str(source)+'broken')
        self.assertEqual(error.exception.code, 404)
        error.exception.close()
        self.plex.config['server'] = 'different-server'
        with self.assertRaises(HTTPError) as error:
            urlopen(str(source))
        self.assertEqual(error.exception.code, 404)
        error.exception.close()

    def test_redirects_and_arbitrary_urls_are_rejected(self):
        source = self.plex.source(self.plex.token('7'))
        self.upstream.redirect = f'http://127.0.0.1:{self.upstream.server_port}/leak'
        with self.assertRaises(HTTPError) as error:
            urlopen(str(source))
        self.assertEqual(error.exception.code, 502)
        error.exception.close()
        self.assertEqual(len(self.upstream.seen), 1)
        for key in ('https://example.org/evil', '//evil/library/parts/1/file', '/library/parts/1/../secret'):
            self.row['Media'][0]['Part'][0]['key'] = key
            with self.assertRaises(ValueError):
                self.plex.source(self.plex.token('7'))

    def test_real_probe_subtitles_burnin_seek_and_offline_conversion(self):
        subtitles = self.root / 'test.srt'
        subtitles.write_text('1\n00:00:00,100 --> 00:00:02,900\nGrüße aus Köln!\n', encoding='utf-8')
        original = self.root / 'original.mkv'
        subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=320x180:rate=20:duration=3',
            '-f', 'lavfi', '-i', 'sine=sample_rate=44100:duration=3', '-i', str(subtitles),
            '-map', '0:v', '-map', '1:a', '-map', '2:s', '-c:v', 'libx264', '-c:a', 'flac', '-c:s', 'srt',
            '-metadata:s:a:0', 'language=jpn', '-metadata:s:s:0', 'language=deu', str(original)], check=True)
        self.upstream.media = original.read_bytes()
        with patch.dict(os.environ, {'PSP_STREAMER_SETTINGS_DIR': str(self.root / 'settings'),
                                    'PSP_STREAMER_DOWNLOAD_DIR': str(self.root / 'downloads'),
                                    'PSP_STREAMER_PASSWORD': ''}):
            with AppServer(('127.0.0.1', 0), Library([])) as app:
                app.plex.close()
                app.plex = app.library.plex = self.plex
                thread = threading.Thread(target=app.serve_forever, daemon=True)
                thread.start()
                def get(path):
                    connection = http.client.HTTPConnection(*app.server_address, timeout=30)
                    try:
                        connection.request('GET', path)
                        reply = connection.getresponse()
                        data = json.loads(reply.read())
                        self.assertEqual(reply.status, 200, data)
                        return data
                    finally:
                        connection.close()
                try:
                    token = self.plex.token('7')
                    info = get('/api/metadata/' + token)
                    self.assertEqual(info['a'][0]['l'], 'jpn')
                    self.assertEqual(info['s'][0]['l'], 'deu')
                    cues = get('/api/subtitles/' + token + '?track=0&timebase=ms')
                    self.assertEqual(cues['t'], 'text')
                    self.assertIn('Grüße', cues['c'][0][2])
                    source = self.plex.source(token)
                    command = ffmpeg_command(source, 0, 'flv', subtitle_track=0, start_seconds=1)
                    output = subprocess.run(command, capture_output=True, timeout=30)
                    self.assertEqual(output.returncode, 0, output.stderr.decode())
                    self.assertTrue(output.stdout.startswith(b'FLV'))
                    job = app.offline.add({'id': token, 'subtitle': 0})
                    until = time.monotonic()+30
                    while time.monotonic()<until:
                        state = app.offline.get(job['job'])
                        if state['state'] in ('ready', 'error', 'cancelled'):
                            break
                        time.sleep(.05)
                    self.assertEqual(state['state'], 'ready', state)
                    self.assertTrue(any(h.get('Range') for _, h in self.upstream.seen))
                    music = self.root / 'music.flac'
                    subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', 'sine=sample_rate=44100:duration=2',
                        '-metadata', 'artist=Test Artist', '-metadata', 'title=Musik', str(music)], check=True)
                    self.upstream.media = music.read_bytes()
                    self.row.update(type='track', title='Musik', grandparentTitle='Test Artist')
                    self.row['Media'][0]['Part'][0].update(file='/unmounted/Musik.flac', key='/library/parts/92/123/file.flac')
                    info = get('/api/metadata/' + token)
                    self.assertEqual(info['artist'], 'Test Artist')
                    self.assertEqual(len(info['a']), 1)
                    job = app.offline.add({'id': token, 'audio_quality': 'v5'})
                    until = time.monotonic()+30
                    while time.monotonic()<until:
                        state = app.offline.get(job['job'])
                        if state['state'] in ('ready', 'error', 'cancelled'):
                            break
                        time.sleep(.05)
                    self.assertEqual(state['state'], 'ready', state)
                    self.assertEqual(state['kind'], 'audio')
                    self.assertTrue(state['name'].endswith('.mp3'))
                finally:
                    app.shutdown()
                    thread.join()

    def test_remote_pgs_stream_copy_and_overlay(self):
        from psp_streamer.server import AppHandler
        from psp_streamer.pgs import parse_pgs
        from types import SimpleNamespace
        def segment(pts, kind, payload):
            return b'PG'+struct.pack('>IIBH', pts, 0, kind, len(payload))+payload
        composition = struct.pack('>HHBHBBBB', 320, 180, 0x10, 0, 0x80, 0, 0, 1)
        composition += struct.pack('>HBBHH', 0, 0, 0, 20, 30)
        clear = struct.pack('>HHBHBBBB', 320, 180, 0x10, 1, 0, 0, 0, 0)
        pixels = bytes([1, 1, 0, 0, 1, 1, 0, 0])
        obj = b'\0\0\0\xc0'+(len(pixels)+4).to_bytes(3, 'big')+struct.pack('>HH', 2, 2)+pixels
        data = (segment(90000, 0x16, composition)+
                segment(90000, 0x17, b'\x01'+struct.pack('>BHHHH', 0, 0, 0, 320, 180))+
                segment(90000, 0x14, bytes([0, 0, 1, 235, 128, 128, 255]))+
                segment(90000, 0x15, obj)+segment(90000, 0x80, b'')+
                segment(225000, 0x16, clear)+segment(225000, 0x80, b''))
        self.assertEqual(len(parse_pgs(data)), 1)
        sup = self.root / 'fixture.sup'
        sup.write_bytes(data)
        original = self.root / 'pgs.mkv'
        subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=320x180:rate=20:duration=3',
            '-i', str(sup), '-map', '0:v', '-map', '1:s', '-c:v', 'libx264', '-c:s', 'copy', str(original)], check=True)
        self.upstream.media = original.read_bytes()
        self.row['updatedAt'] = int(time.time_ns())  # isolate the persistent subtitle cache
        with patch.dict(os.environ, {'PSP_STREAMER_SETTINGS_DIR': str(self.root / 'pgs-settings'),
                                    'PSP_STREAMER_DOWNLOAD_DIR': str(self.root / 'pgs-downloads')}):
            with AppServer(('127.0.0.1', 0), Library([])) as app:
                app.plex.close()
                app.plex = app.library.plex = self.plex
                token = self.plex.token('7')
                cues = AppHandler.pgs_cues(SimpleNamespace(server=app), token, 0)
                self.assertEqual(len(cues), 1)
                self.assertEqual((cues[0].x, cues[0].y, cues[0].width, cues[0].height), (20, 30, 2, 2))
                self.assertEqual(cues[0].pixels, b'\x01'*4)
                source = self.plex.source(token)
                folder = self.root / 'overlay'
                folder.mkdir()
                burn = app.offline._subtitles({'subtitle': 0, 'profile': 'normal', 'state': 'encoding'}, source,
                    {'streams': [{'codec_type': 'subtitle', 'codec_name': 'hdmv_pgs_subtitle'}]}, folder)
                self.assertEqual(burn, -1)
                overlay = (folder / 'subtitles.ovl').read_bytes()
                length = struct.unpack('<I', overlay[4:8])[0]
                self.assertEqual(json.loads(overlay[8:8+length])['t'], 'pgs')

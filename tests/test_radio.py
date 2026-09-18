import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

from psp_streamer.radio import RadioDirectory, resolve_playlist, validate_url, radio_command, IcyLogReader
from psp_streamer.server import AppServer, Library, ffmpeg_command


class RadioTests(unittest.TestCase):
    def test_icy_metadata_partial_lines_encoding_bounds_and_session_ownership(self):
        with tempfile.TemporaryDirectory() as directory:
            radio=RadioDirectory(directory)
            key=radio.change({'name':'Test','url':'https://example.com/live'})['id']
            lease=radio.begin(key)
            reader=IcyLogReader(radio,key,lease)
            reader.feed(b'  icy-name        : NetMD\n[http @ 123] Metadata update for Stream')
            reader.feed("Title: Björk - It\\'s Oh So Quiet\n".encode())
            self.assertEqual(radio.status(key)['radio_station'],'NetMD')
            self.assertEqual(radio.status(key)['radio_title'],"Björk - It's Oh So Quiet")
            reader.feed(b'Metadata update for StreamTitle: M\xf6we\n')
            self.assertEqual(radio.status(key)['radio_title'],'Möwe')
            reader.feed(b'x'*20000+b'Metadata update for StreamTitle: fake\n')
            self.assertEqual(radio.status(key)['radio_title'],'Möwe')
            reader.feed(b'Metadata update for StreamTitle: \n')
            self.assertEqual(radio.status(key)['radio_title'],'')
            newer=radio.begin(key)
            reader.feed(b'Metadata update for StreamTitle: old\n')
            radio.end(key,lease)
            self.assertTrue(radio.status(key)['radio_active'])
            self.assertEqual(radio.status(key)['radio_title'],'')
            radio.end(key,newer)
            self.assertFalse(radio.status(key)['radio_active'])

    def test_directory_is_atomic_persistent_and_bounded(self):
        with tempfile.TemporaryDirectory() as directory:
            radio = RadioDirectory(directory)
            key = radio.change({'name': 'Ö Radio', 'url': 'https://example.com/live'})['id']
            self.assertEqual(RadioDirectory(directory).get(key)['name'], 'Ö Radio')
            radio.change({'id': key, 'name': 'New', 'url': 'http://example.com/stream'})
            self.assertEqual(len(radio.list()), 1)
            before = radio.path.read_bytes()
            with patch('psp_streamer.radio.os.replace', side_effect=OSError('disk full')):
                with self.assertRaises(OSError):
                    radio.change({'id': key, 'name': 'Failed', 'url': 'http://example.com/x'})
            self.assertEqual(radio.path.read_bytes(), before)
            self.assertEqual(radio.get(key)['name'], 'New')
            self.assertNotIn('url', radio.browse(0)['videos'][0])
            for bad in ('file:///etc/passwd', 'concat:http://example.com', 'http://example.com/\n',
                        'https://user:secret@example.com', 'ftp://example.com/x', 'https://x:99999', ''):
                with self.subTest(url=bad), self.assertRaises(ValueError):
                    validate_url(bad)
            for name in ('', 'x\ny', 'ü' * 51):
                with self.assertRaises(ValueError):
                    radio.change({'name': name, 'url': 'https://example.com'})
            radio.change({'id': key, 'delete': True})
            self.assertEqual(RadioDirectory(directory).list(), [])
            with self.assertRaises(ValueError):
                radio.get(key)

    def test_radio_command_preserves_psp_audio_output_and_restricts_input(self):
        for quality in ('96k', '128k', '160k', 'v3', 'v4', 'v5', 'v6'):
            command = radio_command('https://example.com/live', quality, ffmpeg_command)
            regular = ffmpeg_command('https://example.com/live', 0, 'mp3', audio_bitrate=quality)
            begin = command.index('-map')
            self.assertEqual(command[begin:-3], regular[regular.index('-map'):-1])
            self.assertNotIn('-ss', command)
            self.assertEqual(command[command.index('-protocol_whitelist') + 1], 'http,https,tcp,tls')
            self.assertEqual(command[command.index('-tls_verify') + 1], '1')
            self.assertLess(command.index('-rw_timeout'), command.index('-i'))


class RadioIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mp3 = subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i',
            'sine=frequency=440:sample_rate=44100', '-t', '2', '-ac', '2', '-c:a', 'libmp3lame',
            '-b:a', '128k', '-write_xing', '0', '-id3v2_version', '0', '-f', 'mp3', 'pipe:1'],
            capture_output=True, check=True, timeout=10).stdout
        class Sender(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                if self.path == '/redirect.m3u':
                    self.send_response(302)
                    self.send_header('Location', '/list.m3u')
                    self.end_headers()
                    return
                contents = {'/list.m3u': b'#EXTM3U\n#EXTINF:-1,Test\n/live\n',
                            '/list.pls': b'[playlist]\nFile2=http://invalid.invalid/\nFile1=/live\n',
                            '/bad.m3u': b'file:///etc/passwd\n',
                            '/cycle.m3u': b'/cycle.m3u\n',
                            '/hls.m3u': b'#EXTM3U\n#EXT-X-TARGETDURATION:2\n/live\n',
                            '/large.m3u': b'x' * 65537}
                if self.path in contents:
                    self.send_response(200)
                    self.send_header('Content-Length', str(len(contents[self.path])))
                    self.end_headers()
                    self.wfile.write(contents[self.path])
                    return
                if self.path not in ('/live', '/stall', '/icy'):
                    self.send_error(404)
                    return
                if self.path == '/icy':
                    # SHOUTcast's historical status line, UTF-8 metadata like
                    # HA-NetMD, and title changes on the SAME input connection.
                    self.wfile.write(b'ICY 200 OK\r\nContent-Type: audio/mpeg\r\nicy-metaint: 2048\r\nicy-name: MiniDisc Player\r\n\r\n')
                    offset=0
                    count=0
                    try:
                        while not cls.sender_stop.is_set():
                            self.wfile.write((cls.mp3*2)[offset:offset+2048])
                            offset=(offset+2048)%len(cls.mp3)
                            title="First title" if count<3 else "Björk - It\\'s Oh So Quiet"
                            metadata=("StreamTitle='"+title+"';").encode()
                            metadata+=b'\0'*(-len(metadata)%16)
                            self.wfile.write(bytes([len(metadata)//16])+metadata)
                            self.wfile.flush();count+=1;time.sleep(.025)
                    except OSError:
                        pass
                    return
                self.send_response(200)
                self.send_header('Content-Type', 'audio/mpeg')
                self.end_headers()
                try:
                    while not cls.sender_stop.is_set():
                        for pos in range(0, len(cls.mp3), 2048):
                            if cls.sender_stop.is_set():
                                return
                            self.wfile.write(cls.mp3[pos:pos + 2048])
                            self.wfile.flush()
                            time.sleep(.025)
                        if self.path == '/stall':
                            cls.sender_stop.wait(10)
                            return
                except (OSError, ConnectionError):
                    pass
        cls.sender_stop = threading.Event()
        cls.sender = ThreadingHTTPServer(('127.0.0.1', 0), Sender)
        cls.sender_thread = threading.Thread(target=cls.sender.serve_forever)
        cls.sender_thread.start()
        cls.base = f'http://127.0.0.1:{cls.sender.server_port}'

    @classmethod
    def tearDownClass(cls):
        cls.sender_stop.set()
        cls.sender.shutdown()
        cls.sender.server_close()
        cls.sender_thread.join()

    def test_bounded_playlist_resolution(self):
        for path in ('list.m3u', 'list.pls', 'redirect.m3u'):
            self.assertEqual(resolve_playlist(self.base + '/' + path), self.base + '/live')
        for path in ('bad.m3u', 'cycle.m3u', 'hls.m3u', 'large.m3u', 'hls.m3u8'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                resolve_playlist(self.base + '/' + path)

    def test_server_directory_remote_and_real_transcoding(self):
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ, {
                'PSP_STREAMER_RADIO_DIR': directory, 'PSP_STREAMER_SETTINGS_DIR': directory,
                'PSP_STREAMER_DOWNLOAD_DIR': directory + '/downloads',
                'PSP_STREAMER_PASSWORD': '', 'MAX_TRANSCODES': '1'}):
            with AppServer(('127.0.0.1', 0), Library([Path(directory)])) as server:
                thread = threading.Thread(target=server.serve_forever)
                thread.start()
                def request(method, path, body=None):
                    connection = http.client.HTTPConnection(*server.server_address, timeout=10)
                    connection.request(method, path, json.dumps(body) if body is not None else None,
                                       {'Content-Type': 'application/json'})
                    response = connection.getresponse()
                    status, result = response.status, json.loads(response.read() or b'{}')
                    connection.close()
                    return status, result
                try:
                    status, station = request('POST', '/api/radio', {'name': 'Test Radio', 'url': self.base + '/list.m3u'})
                    self.assertEqual(status, 200)
                    key = station['id']
                    listing = request('GET', '/api/library')[1]
                    self.assertEqual(listing['folders'][0]['path'], ':radio:')
                    stations = request('GET', '/api/library?path=%3Aradio%3A')[1]['videos']
                    self.assertEqual(stations[0]['id'], key)
                    self.assertTrue(stations[0]['live'])
                    metadata = request('GET', '/api/metadata/' + key)[1]
                    self.assertEqual(metadata['d'], '0')
                    self.assertEqual(metadata['s'], [])
                    self.assertEqual(request('GET', '/api/media-next/' + key)[1], {})
                    command = request('POST', '/api/remote/command', {'action': 'play', 'id': key, 'start': 99, 'subtitle': 0})[1]
                    self.assertEqual((command['start'], command['subtitle'], command['kind']), (0, -1, 'audio'))
                    self.assertNotIn('url', command)
                    self.assertEqual(request('GET', '/api/transcode/' + key + '?container=flv')[0], 400)
                    self.assertEqual(request('GET', '/api/transcode/' + key + '?container=mp3&start=30')[0], 400)
                    # Real HTTP sender -> FFmpeg -> server -> MP3 bytes. Close
                    # twice and verify no stale process monopolises the slot.
                    for quality in ('128k', 'v5', '160k'):
                        if quality=='160k':
                            request('POST','/api/radio',{'id':key,'name':'Test Radio','url':self.base+'/icy'})
                        connection = http.client.HTTPConnection(*server.server_address, timeout=12)
                        connection.request('GET', f'/api/transcode/{key}?container=mp3&audio_quality={quality}')
                        response = connection.getresponse()
                        self.assertEqual(response.status, 200)
                        audio = response.read(4096)
                        self.assertEqual(len(audio), 4096)
                        self.assertEqual(audio[0], 255)
                        self.assertEqual(audio[1] & 224, 224)
                        if quality=='160k':
                            state=request('GET','/api/radio/status/'+key)[1]
                            self.assertEqual(state['radio_station'],'MiniDisc Player')
                            self.assertEqual(state['radio_title'],"Björk - It's Oh So Quiet")
                            remote=request('GET','/api/remote/next?after=999&radio='+key)[1]
                            self.assertEqual(remote['radio_title'],state['radio_title'])
                        response.close()
                        connection.close()
                        self.assertTrue(server.transcode_slots.acquire(timeout=6), 'Radio slot leaked after client disconnected')
                        server.transcode_slots.release()
                    # ID3 comes from real ffprobe, not filename inference.
                    song=Path(directory)/'tagged.mp3'
                    subprocess.run(['ffmpeg','-v','error','-f','lavfi','-i','sine=duration=1',
                                    '-metadata','title=Über den Wolken','-metadata','artist=Test Artist',
                                    '-metadata','album=Test Album',str(song)],check=True,timeout=10)
                    from psp_streamer.server import MediaItem
                    song_id=server.library.encode(MediaItem(0,song.name))
                    tags=request('GET','/api/metadata/'+song_id)[1]
                    self.assertEqual((tags['title'],tags['artist'],tags['album']),
                                     ('Über den Wolken','Test Artist','Test Album'))
                    self.assertEqual(tags['name'],'Test Artist - Über den Wolken')
                    # Management shares password protection with media APIs.
                    server.settings.bootstrap = b'secret'
                    self.assertEqual(request('GET', '/api/radio')[0], 401)
                finally:
                    server.shutdown()
                    thread.join()

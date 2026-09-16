import hashlib
import io
import http.client
import json
import os
import struct
import subprocess
import tempfile
import threading
import time
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

from psp_streamer.server import AppServer, Library, MediaItem


class OfflineTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.env = patch.dict('os.environ', {'PSP_STREAMER_DOWNLOAD_DIR': str(self.root / 'cache'), 'PSP_STREAMER_PASSWORD': ''})
        self.env.start()
        self.library = Library([self.root])
        self.server = AppServer(('127.0.0.1', 0), self.library)
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.env.stop()
        self.tmp.cleanup()

    def request(self, method, path, body=None, headers=None):
        conn = http.client.HTTPConnection(*self.server.server_address, timeout=10)
        opts = {'Content-Type': 'application/json'} if body is not None else {}
        opts.update(headers or {})
        conn.request(method, path, json.dumps(body) if body is not None else None, opts)
        reply = conn.getresponse()
        result = reply.status, dict(reply.getheaders()), reply.read()
        conn.close()
        return result

    def source(self):
        source = self.root / 'Episode {ä} S01E01.mkv'
        subtitle = self.root / 'sub.srt'
        subtitle.write_text('1\n00:00:00,200 --> 00:00:01,900\nGrüße aus Köln!\n')
        subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=480x272:rate=20:duration=4',
                        '-f', 'lavfi', '-i', 'sine=sample_rate=44100:duration=4', '-i', str(subtitle),
                        '-map', '0:v', '-map', '1:a', '-map', '2:s', '-c:v', 'libx264', '-c:a', 'pcm_s16le',
                        '-c:s', 'srt', str(source)], check=True)
        return self.library.encode(MediaItem(0, source.name))

    def wait_ready(self, key):
        until = time.monotonic() + 30
        while time.monotonic() < until:
            job = self.server.offline.get(key)
            if job['state'] in ('ready', 'error', 'cancelled'):
                self.assertEqual(job['state'], 'ready', job)
                return job
            time.sleep(.05)
        self.fail('Conversion did not finish')

    def test_web_preferred_track_matching(self):
        script = (Path(__file__).resolve().parents[1] / 'static/offline.js').read_text()
        function = script[script.index('function restoreTrack('):script.index('choose=async')]
        subprocess.run(['node', '-e', function + '''
const assert=require('node:assert/strict');
const select={options:[{value:'-1',textContent:'Off'},
 {value:'3',textContent:'deu Deutsch'}, {value:'1',textContent:'jpn Japanese'}],value:'-1'};
assert.equal(restoreTrack(select,'ger German'),true);
assert.equal(select.value,'3');
assert.equal(restoreTrack(select,'jpn Japanese'),true);
assert.equal(select.value,'1');
assert.equal(restoreTrack(select,'Off'),true);
assert.equal(select.value,'-1');
assert.equal(restoreTrack(select,'eng English'),false);
assert.equal(select.value,'-1');
'''], check=True)

    def test_pc_export_and_persistent_preferences(self):
        prefs = {'audio': 'jpn Japanese', 'subtitle': 'ger Deutsch', 'profile': 'tv',
                 'audio_quality': 'v5', 'video_fps': '24000/1001'}
        code, _, body = self.request('POST', '/api/offline/preferences', prefs)
        self.assertEqual(code, 200, body)
        self.assertEqual(json.loads(self.request('GET', '/api/offline/preferences')[2]), prefs)
        self.assertEqual(json.loads((self.root / 'cache/preferences.json').read_text()), prefs)
        self.assertEqual(self.request('POST', '/api/offline/preferences', {'profile': '../bad'})[0], 400)
        self.assertEqual(self.request('GET', '/api/offline/export/unknown')[0], 400)
        token = self.source()
        _, _, body = self.request('POST', '/api/offline/jobs', {'id': token, 'subtitle': 0})
        key = json.loads(body)['job']
        job = self.wait_ready(key)
        code, headers, body = self.request('GET', '/api/offline/export/' + key)
        self.assertEqual(code, 200)
        self.assertEqual(headers['Content-Type'], 'application/zip')
        prefix = f'PSP/VIDEO/PSPStreamer/{key}/'
        with zipfile.ZipFile(io.BytesIO(body)) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(archive.namelist(), [prefix + f['name'] for f in job['files']] +
                             [prefix + 'job.json', prefix + 'ready'])
            for entry in job['files']:
                data = archive.read(prefix + entry['name'])
                self.assertEqual(len(data), entry['size'])
                self.assertEqual(hashlib.sha256(data).hexdigest(), entry['sha256'])
            manifest = archive.read(prefix + 'job.json')
            self.assertIn(b'"name":"', manifest)
            self.assertEqual(json.loads(manifest), job)
            self.assertEqual(archive.read(prefix + 'ready'), b'1')
        self.assertFalse(list((self.root / 'cache').rglob('*.zip')))
        source = self.root / 'cache' / key / job['files'][0]['name']
        with source.open('r+b') as stream:
            stream.write(b'BAD')
        _, _, body = self.request('GET', '/api/offline/export/' + key)
        with zipfile.ZipFile(io.BytesIO(body)) as archive:
            self.assertNotIn(prefix + 'ready', archive.namelist())

    def test_queue_variants_conversion_resume_subtitles_seek_and_restart(self):
        token = self.source()
        keys = []
        root = Path(__file__).resolve().parents[1]
        header = (root / 'psp-client/timed_stream.h').read_text()
        harness = (root / 'tests/offline_reader_harness.c').read_text()
        harness = harness.replace('/* TIMED_READ */', header[header.index('static int timed_read('):header.index('static int timed_connect(')])
        harness = harness.replace('/* TIMED_READER */', header[header.index('static int timed_reader('):])
        (self.root / 'reader.c').write_text(harness)
        reader = self.root / 'reader'
        subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-fsanitize=undefined',
                        '-I', str(root / 'psp-client'), str(self.root / 'reader.c'), '-o', str(reader)], check=True)
        client_header = (root / 'psp-client/offline_ui.h').read_text()
        client = (root / 'tests/offline_http_harness.c').read_text().replace('/* OFFLINE_HTTP */',
                  client_header[client_header.index('static int offline_connect('):client_header.index('static int offline_hash(')])
        (self.root / 'download.c').write_text(client)
        downloader = self.root / 'downloader'
        subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined', '-I', str(root / 'tests'),
                        str(self.root / 'download.c'), '-o', str(downloader)], check=True)
        for profile, sub in (('normal', 0), ('tv', -1)):
            code, _, body = self.request('POST', '/api/offline/jobs', {'id': token, 'profile': profile, 'subtitle': sub})
            self.assertEqual(code, 200, body)
            keys.append(json.loads(body)['job'])
        self.assertNotEqual(*keys)
        for key, profile in zip(keys, ('normal', 'tv')):
            job = self.wait_ready(key)
            self.assertEqual(job['profile'], profile)
            self.assertEqual(job['name'], 'Episode {ä} S01E01.flv')
            self.assertEqual(len(job['files']), 3)
            movie = self.root / f'{profile}.flv'
            code, headers, first = self.request('GET', f'/api/offline/file/{key}/0')
            self.assertEqual(code, 200)
            self.assertEqual(len(first), job['files'][0]['size'])
            code, headers, rest = self.request('GET', f'/api/offline/file/{key}/0', headers={'Range': 'bytes=117-'})
            self.assertEqual(code, 206)
            self.assertEqual(first[117:], rest)
            self.assertEqual(hashlib.sha256(first[:117]+rest).hexdigest(), job['files'][0]['sha256'])
            # Actual PSP downloader, using native socket/file shims, resumes
            # an interrupted byte prefix and rejects a JSON error response.
            partial = self.root / 'download.part'
            partial.write_bytes(first[:117])
            subprocess.run([str(downloader), str(self.server.server_port), f'/api/offline/file/{key}/0',
                            str(partial), str(len(first)), '0'], check=True)
            self.assertEqual(partial.read_bytes(), first)
            partial.write_bytes(first[:117])
            subprocess.run([str(downloader), str(self.server.server_port), '/api/offline/file/unknown/0',
                            str(partial), str(len(first)), '-1'], check=True)
            self.assertEqual(partial.read_bytes(), first[:117])
            self.assertEqual(headers['Content-Range'], f'bytes 117-{len(first)-1}/{len(first)}')
            for invalid in ('bytes=999999999999-', 'bytes=-5', 'bytes=1-2,3-4'):
                self.assertEqual(self.request('GET', f'/api/offline/file/{key}/0', headers={'Range': invalid})[0], 416)
            movie.write_bytes(first)
            streams = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-show_streams', '-of', 'json', str(movie)]))['streams']
            self.assertEqual(streams[0]['profile'], 'Main')
            self.assertEqual(streams[0]['width'], 480 if profile == 'normal' else 720)
            subprocess.run(['ffmpeg', '-v', 'error', '-xerror', '-i', str(movie), '-f', 'null', '-'], check=True)
            ovl = self.request('GET', f'/api/offline/file/{key}/1')[2]
            self.assertEqual(ovl[:4], b'OVL1')
            size = struct.unpack('<I', ovl[4:8])[0]
            cues = json.loads(ovl[8:8+size])
            self.assertEqual(bool(cues['c']), profile == 'normal')
            if cues['c']:
                self.assertIn('Grüße', cues['c'][0][2])
            index = self.request('GET', f'/api/offline/file/{key}/2')[2]
            entries = list(struct.iter_unpack('<II', index))
            self.assertGreaterEqual(len(entries), 2)
            for offset in (0, entries[-1][1]):
                raw_video, raw_audio = self.root / 'raw.h264', self.root / 'raw.mp3'
                subprocess.run([str(reader), str(movie), str(offset), str(raw_video), str(raw_audio)], check=True, capture_output=True)
                for raw in (raw_video, raw_audio):
                    subprocess.run(['ffmpeg', '-v', 'error', '-xerror', '-i', str(raw), '-f', 'null', '-'], check=True)
            for pts, offset in entries:
                self.assertEqual(first[offset], 9)
                self.assertEqual(first[offset+11:offset+13], b'\x17\x01')
                self.assertEqual(pts, int.from_bytes(first[offset+4:offset+7], 'big') | first[offset+7]<<24)
            # Header + AVC configuration + indexed keyframe decodes independently.
            at = 13
            while first[at] != 9:
                at += 15 + int.from_bytes(first[at+1:at+4], 'big')
            end = at + 15 + int.from_bytes(first[at+1:at+4], 'big')
            seek = self.root / 'seek.flv'
            seek.write_bytes(first[:13]+first[at:end]+first[entries[-1][1]:])
            subprocess.run(['ffmpeg', '-v', 'error', '-xerror', '-i', str(seek), '-f', 'null', '-'], check=True)
        self.server.offline.close()
        from psp_streamer.offline import OfflineQueue
        old = self.server.offline
        recovered = OfflineQueue(old.root, self.library, old.command_builder, old.parse_cues, old.slots)
        self.assertEqual(recovered.get(keys[0])['state'], 'ready')
        self.assertEqual(self.request('POST', '/api/offline/delete', {'job': keys[0]})[0], 200)
        self.assertEqual(self.request('GET', f'/api/offline/file/{keys[0]}/0')[0], 400)
        self.assertTrue((old.root / keys[1]).exists())

    def test_validation_cancel_and_no_path_access(self):
        source = self.root / 'dummy.mkv';source.touch()
        token = self.library.encode(MediaItem(0, source.name))
        for options in ({'profile':'../../etc'}, {'audio':-1}, {'subtitle':100}, {'video_fps':'60'}, {'audio_quality':'evil'}):
            self.assertEqual(self.request('POST', '/api/offline/jobs', dict(id=token, **options))[0], 400)
        slots = self.server.transcode_slots
        for _ in range(4):slots.acquire()
        try:
            job = json.loads(self.request('POST', '/api/offline/jobs', {'id':token})[2])
            self.assertEqual(self.request('GET', f"/api/offline/file/{job['job']}/0")[0], 400)
            self.assertEqual(self.request('POST', '/api/offline/cancel', {'job':job['job']})[0], 200)
            self.assertEqual(self.server.offline.get(job['job'])['state'], 'cancelled')
        finally:
            for _ in range(4):slots.release()
        self.assertEqual(self.request('POST', '/api/offline/delete', {'job':'../../media'})[0], 400)
        self.assertTrue(source.exists())
        self.assertEqual(self.request('POST', '/api/offline/jobs', {'id':token}, headers={'Origin':'https://evil.invalid'})[0], 403)

    @unittest.skipUnless(os.environ.get('MBEDTLS_HOST_SOURCE') and os.environ.get('MBEDTLS_HOST_BUILD'), 'Native SHA256 worker test needs host mbedTLS')
    def test_actual_psp_worker_space_resume_checksums_and_ready_commit(self):
        root = Path(__file__).resolve().parents[1]
        client = (root / 'psp-client/offline_ui.h').read_text()
        main = (root / 'psp-client/main.c').read_text()
        code = (root / 'tests/offline_http_harness.c').read_text().split('int main(')[0]
        code = code.replace('/* OFFLINE_HTTP */', client[client.index('static int offline_connect('):client.index('static int offline_hash(')])
        worker = (root / 'tests/offline_worker_harness.c').read_text()
        value = main[main.index('static int json_value(const char *from, const char *key, char *destination, size_t length) {'):]
        value = value[:value.index('\n}\n')+3]
        integer = main[main.index('static int json_integer(const char *from, const char *key, int fallback) {'):]
        integer = integer[:integer.index('\n}\n')+3]
        worker = worker.replace('/* JSON_HELPERS */', value+integer)
        worker = worker.replace('/* FILE_HELPERS */', client[client.index('static int offline_key_valid('):client.index('static unsigned long long offline_size(')])
        worker = worker.replace('/* HASH_AND_WORKER */', client[client.index('static int offline_hash('):client.index('static int offline_transfer(')])
        source = self.root / 'worker.c';source.write_text(code+worker)
        binary = self.root / 'worker'
        subprocess.run(['cc','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=undefined',
                        '-I',str(root/'tests'),'-I',str(Path(os.environ['MBEDTLS_HOST_SOURCE'])/'include'),
                        str(source),'-L',str(Path(os.environ['MBEDTLS_HOST_BUILD'])/'library'),'-lmbedcrypto','-o',str(binary)],check=True)
        token = self.source()
        job = self.wait_ready(self.server.offline.add({'id':token,'subtitle':0})['job'])
        stick = self.root / 'stick';stick.mkdir()
        # Low space leaves metadata/partial state, never a ready marker.
        subprocess.run([str(binary),str(self.server.server_port),job['job'],str(stick),'1'],check=True)
        folder=stick/job['job'];self.assertFalse((folder/'ready').exists())
        movie=(self.server.offline.root/job['job']/job['name']).read_bytes()
        (folder/(job['name']+'.part')).write_bytes(movie[:117])
        subprocess.run([str(binary),str(self.server.server_port),job['job'],str(stick),'0'],check=True)
        self.assertEqual((folder/'ready').read_bytes(),b'1')
        for entry in job['files']:
            self.assertEqual(hashlib.sha256((folder/entry['name']).read_bytes()).hexdigest(),entry['sha256'])
        # Valid completed files are kept even if no new space is available.
        subprocess.run([str(binary),str(self.server.server_port),job['job'],str(stick),'0'],check=True)
        # A full-length but damaged partial is not committed or left playable.
        (folder/job['name']).write_bytes(b'!'*len(movie))
        (folder/(job['name']+'.part')).write_bytes(b'?'*len(movie))
        subprocess.run([str(binary),str(self.server.server_port),job['job'],str(stick),'2'],check=True)
        self.assertFalse((folder/'ready').exists())
        self.assertFalse((folder/(job['name']+'.part')).exists())
        subprocess.run([str(binary),str(self.server.server_port),job['job'],str(stick),'0'],check=True)
        self.assertEqual((folder/job['name']).read_bytes(),movie)

    def test_bitmap_package_retains_palette_and_tv_burn_policy(self):
        from psp_streamer.pgs import PgsCue
        folder=self.root/'package';folder.mkdir()
        source=self.root/'pgs.mkv';source.touch()
        cue=PgsCue(1,2,10,20,2,2,b'\x01\x02\x03\x04',bytes(range(256))*4,1920,1080)
        job={'subtitle':0,'profile':'normal'}
        probe={'streams':[{'codec_type':'subtitle','codec_name':'hdmv_pgs_subtitle'}]}
        def capture(command, *args):
            if command[0]=='mkvmerge':return b'{"tracks":[{"id":2,"type":"subtitles"}]}'
            (folder/'extract.sup').write_bytes(b'fixture')
            return b''
        with patch.object(self.server.offline,'_capture',side_effect=capture), patch('psp_streamer.pgs.parse_pgs',return_value=[cue]):
            self.assertEqual(self.server.offline._subtitles(job,source,probe,folder),-1)
        blob=(folder/'subtitles.ovl').read_bytes();length=struct.unpack('<I',blob[4:8])[0]
        payload=json.loads(blob[8:8+length]);self.assertEqual(payload['t'],'pgs')
        self.assertEqual(payload['c'],[[1000,2000,10,20,2,2,1920,1080]])
        self.assertEqual(blob[8+length:],cue.palette+cue.pixels)
        job['profile']='tv'
        self.assertEqual(self.server.offline._subtitles(job,source,probe,folder),0)


if __name__ == '__main__':
    unittest.main()

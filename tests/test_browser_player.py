"""Only the new browser mux, HTTP isolation and process lifecycle."""
import http.client
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

from psp_streamer.browser_player import browser_command
from psp_streamer.server import AppServer, Library, MediaItem, ffmpeg_command


class BrowserCommandTests(unittest.TestCase):
    def test_mux_does_not_change_psp_command(self):
        original = ffmpeg_command(Path('input.mkv'), 1, 'flv', audio_bitrate='v5')
        command = browser_command(original, False)
        self.assertEqual(original[original.index('-c:a') + 1], 'libmp3lame')
        self.assertEqual(command[command.index('-c:a') + 1], 'aac')
        self.assertEqual(command[command.index('-f') + 1], 'mp4')
        self.assertNotIn('-q:a', command)
        self.assertNotIn('fps=', command[command.index('-vf') + 1])
        self.assertIn('empty_moov', command[command.index('-movflags') + 1])

    def test_bitmap_and_text_filter_preserved(self):
        for bitmap in (False, True):
            command = browser_command(ffmpeg_command(Path('input.mkv'), 0, 'flv',
                subtitle_track=0, bitmap_subtitle=bitmap, subtitle_source=Path('/tmp/sub.srt')), False)
            self.assertIn('overlay=' if bitmap else 'subtitles=', ' '.join(command))


class BrowserHttpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temp.name)
        subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=160x90:rate=24',
                        '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=44100',
                        '-t', '6', '-c:v', 'libx264', '-c:a', 'aac', str(cls.root/'sample.mkv')], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.env = patch.dict(os.environ, {'PSP_STREAMER_SETTINGS_DIR': str(self.root/'state'), 'MAX_TRANSCODES': '1'})
        self.env.start()
        self.server = AppServer(('127.0.0.1', 0), Library([self.root]))
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.start()
        self.token = self.server.library.encode(MediaItem(0, 'sample.mkv'))

    def tearDown(self):
        self.server.shutdown()
        self.thread.join()
        self.server.server_close()
        self.env.stop()

    def test_real_mp4_and_psp_state_isolation(self):
        connection = http.client.HTTPConnection(*self.server.server_address, timeout=30)
        connection.request('GET', '/api/browser-stream/'+self.token+'?profile=tv&start=2')
        response = connection.getresponse()
        self.assertEqual(response.status, 200)
        self.assertEqual(response.getheader('Content-Type'), 'video/mp4')
        data = response.read()
        connection.close()
        self.assertIn(b'ftyp', data[:32])
        path = self.root/'result.mp4'
        path.write_bytes(data)
        result = subprocess.run(['ffprobe', '-v', 'error', '-show_streams', '-show_format', '-of', 'json', str(path)], capture_output=True, check=True)
        info = json.loads(result.stdout)
        self.assertEqual([s['codec_name'] for s in info['streams']], ['h264', 'aac'])
        self.assertAlmostEqual(float(info['format']['duration']), 4, delta=.2)
        self.assertEqual(self.server.remote_sequence, 0)
        self.assertEqual(self.server.stream_pauses.active, {})

    def test_client_progress_http_keeps_psp_state_and_commands(self):
        self.server.player_status.report({'media':[self.token],'state':['playing'],'position':['4000'],'duration':['6000']})
        command=self.server.set_remote_command({'action':'pause'})
        before=self.server.player_status.snapshot()
        con=http.client.HTTPConnection(*self.server.server_address,timeout=10)
        for sequence,state in enumerate(('playing','paused','stopped'),1):
            body=json.dumps(dict(client='browser-fixture',sequence=sequence,id=self.token,
                state=state,position=1500,duration=6000,name='Fixture'))
            con.request('POST','/api/client-playback',body,{'Content-Type':'application/json'})
            response=con.getresponse();self.assertEqual(response.status,200);response.read()
        self.assertEqual(self.server.player_status.snapshot()['position'],before['position'])
        self.assertEqual(self.server.remote_after(0),command)
        self.assertEqual(self.server.comfort.snapshot()['records'][0]['seconds'],1)
        con.request('GET','/api/xbox/metadata/'+self.token,headers={'X-PSP-Web':'1'})
        response=con.getresponse();self.assertEqual(response.status,200)
        self.assertEqual(json.loads(response.read())['resume'],1)
        con.close()

    def test_stop_releases_encoder_slot(self):
        connection = http.client.HTTPConnection(*self.server.server_address, timeout=30)
        connection.request('GET', '/api/browser-stream/'+self.token)
        response = connection.getresponse()
        self.assertEqual(response.status, 200)
        response.read(1024)
        response.close()
        connection.close()
        deadline = time.monotonic()+5
        while time.monotonic()<deadline:
            if self.server.transcode_slots.acquire(blocking=False):
                self.server.transcode_slots.release()
                break
            time.sleep(.05)
        else:
            self.fail('Browser Stop did not release the encoder')

    def test_validation_and_audio(self):
        connection = http.client.HTTPConnection(*self.server.server_address, timeout=30)
        for options in ('audio=32', 'start=nan', 'subtitle=32'):
            connection.request('GET', '/api/browser-stream/'+self.token+'?'+options)
            response = connection.getresponse()
            self.assertEqual(response.status, 400)
            response.read()
        connection.request('GET', '/api/browser-stream/'+self.token+'?kind=audio&start=5')
        response = connection.getresponse()
        self.assertEqual(response.status, 200)
        self.assertEqual(response.getheader('Content-Type'), 'audio/mpeg')
        self.assertGreater(len(response.read()), 1000)
        connection.close()

    def test_browser_endpoint_requires_existing_login(self):
        self.server.settings.bootstrap = b'fixture-password'
        connection = http.client.HTTPConnection(*self.server.server_address, timeout=10)
        connection.request('GET', '/api/browser-stream/'+self.token)
        response = connection.getresponse()
        self.assertEqual(response.status, 401)
        response.read()
        # Native media elements authenticate via the existing HttpOnly cookie.
        session, _ = self.server.web_sessions.create()
        connection.request('GET', '/api/browser-stream/'+self.token+'?kind=audio&start=5',
                           headers={'Cookie': 'psp_session='+session})
        response = connection.getresponse()
        self.assertEqual(response.status, 200)
        response.read()
        connection.close()

    @unittest.skipUnless(os.environ.get('PLAYWRIGHT_MODULE'), 'optional actual browser playback')
    def test_browser_controls(self):
        import shutil
        for season in (1,2):
            target=self.root/f'Show/Season {season}/Episode.mkv';target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(self.root/'sample.mkv',target)
        subprocess.run(['node', 'tests/browser_player.cjs'], check=True, timeout=65,
                       env={**os.environ, 'BROWSER_TEST_URL': 'http://%s:%s' % self.server.server_address,
                            'BROWSER_TEST_TOKEN': self.token,
                            'BROWSER_SEASON_TOKEN':self.server.library.encode(MediaItem(0,'Show/Season 1/Episode.mkv')),
                            'BROWSER_NEXT_TOKEN':self.server.library.encode(MediaItem(0,'Show/Season 2/Episode.mkv'))})


if __name__ == '__main__':
    unittest.main()

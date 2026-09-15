import re
import json
import http.client
import threading
import subprocess
import tempfile
import unittest
from pathlib import Path

from psp_streamer.server import ffmpeg_command, AppServer, Library, MediaItem

ROOT = Path(__file__).resolve().parents[1]


class StreamOptionsTests(unittest.TestCase):
    def test_remote_option_validation_and_forwarding(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'film.mkv').touch()
            library = Library([root])
            with AppServer(('127.0.0.1', 0), library) as server:
                worker = threading.Thread(target=server.serve_forever)
                worker.start()
                connection = http.client.HTTPConnection(*server.server_address, timeout=3)
                try:
                    for quality in ('96k', '128k', '160k', 'v6', 'v5', 'v4', 'v3'):
                        for fps in ('20', '24000/1001'):
                            command = dict(action='play', id=library.encode(MediaItem(0, 'film.mkv')),
                                           audio_quality=quality, video_fps=fps)
                            connection.request('POST', '/api/remote/command', json.dumps(command),
                                               {'Content-Type': 'application/json'})
                            reply = connection.getresponse()
                            self.assertEqual(reply.status, 200)
                            data = json.loads(reply.read())
                            self.assertEqual(data['audio_quality'], quality)
                            self.assertEqual(data['video_fps'], fps)
                    for field, value in (('video_fps', '60'), ('audio_quality', 'v0'), ('audio_quality', [])):
                        invalid = {**command, field: value}
                        connection.request('POST', '/api/remote/command', json.dumps(invalid),
                                           {'Content-Type': 'application/json'})
                        reply = connection.getresponse()
                        self.assertEqual(reply.status, 400)
                        reply.read()
                finally:
                    connection.close()
                    server.shutdown()
                    worker.join()

    def test_actual_flv_film_cadence_for_lcd_and_tv(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'film.mkv'
            subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i',
                            'testsrc2=size=720x480:rate=24000/1001:duration=2',
                            '-f', 'lavfi', '-i', 'sine=sample_rate=44100:duration=2',
                            '-c:v', 'libx264', '-preset', 'ultrafast', '-c:a', 'pcm_s16le',
                            str(source)], check=True)
            for tv in (False, True):
                command = ffmpeg_command(source, 0, 'flv', audio_bitrate='v5',
                                         tv_output=tv, video_fps='24000/1001')
                command.remove('-re')
                index = command.index('-readrate_initial_burst')
                del command[index:index+2]
                encoded = subprocess.check_output(command)
                probe = json.loads(subprocess.check_output([
                    'ffprobe', '-v', 'error', '-show_packets', '-show_streams',
                    '-of', 'json', 'pipe:0'], input=encoded))
                stream = next(s for s in probe['streams'] if s['codec_type'] == 'video')
                self.assertEqual((stream['width'], stream['height']), (720,480) if tv else (480,272))
                video = [int(p['pts']) for p in probe['packets'] if p['codec_type'] == 'video']
                self.assertEqual(len(video), 48)
                self.assertTrue(all(b-a in (41,42) for a,b in zip(video,video[1:])))
                audio = [int(p['pts']) for p in probe['packets'] if p['codec_type'] == 'audio']
                self.assertTrue(all(b-a in (26,27) for a,b in zip(audio,audio[1:])))

    def test_encoder_options_and_legacy_defaults(self):
        for container in ('mp3', 'flv'):
            for quality in ('96k', '128k', '160k', 'v6', 'v5', 'v4', 'v3'):
                command = ffmpeg_command(Path('example.mkv'), 0, container,
                                         audio_bitrate=quality)
                key = '-q:a' if quality.startswith('v') else '-b:a'
                self.assertEqual(command[command.index(key) + 1],
                                 quality[1:] if quality.startswith('v') else quality)
                self.assertNotIn('-b:a' if key == '-q:a' else '-q:a', command)
        for tv in (False, True):
            for fps in ('20', '24000/1001'):
                command = ffmpeg_command(Path('example.mkv'), 0, 'flv',
                                         tv_output=tv, video_fps=fps)
                self.assertIn('fps=' + fps + ',', ' '.join(command))
                self.assertIn('720:480' if tv else '480:272', ' '.join(command))
        legacy = ffmpeg_command(Path('example.mkv'), 0, 'h264', video_fps='24000/1001')
        self.assertIn('fps=201/10,', ' '.join(legacy))

    def test_real_vbr_frames_fit_client_and_have_constant_sample_duration(self):
        source = (ROOT / 'psp-client/main.c').read_text()
        limit = int(re.search(r'#define MP3_MAX_FRAME_BYTES (\d+)', source)[1])
        for quality in ('3', '4', '5', '6'):
            data = subprocess.check_output([
                'ffmpeg', '-v', 'error', '-f', 'lavfi', '-i',
                'anoisesrc=duration=2:sample_rate=44100:seed=123',
                '-ac', '2', '-c:a', 'libmp3lame', '-q:a', quality,
                '-write_xing', '0', '-id3v2_version', '0', '-f', 'mp3', 'pipe:1'])
            offset, sizes = 0, set()
            bitrates = (0,32,40,48,56,64,80,96,112,128,160,192,224,256,320)
            while offset < len(data):
                header = data[offset:offset+4]
                self.assertEqual(header[0], 255)
                self.assertEqual(header[1] & 254, 250)
                self.assertEqual(header[2] & 12, 0)  # MPEG-1, 44100 Hz
                size = 144 * bitrates[header[2] >> 4] * 1000 // 44100 + ((header[2] >> 1) & 1)
                self.assertLessEqual(size, limit)
                sizes.add(size)
                offset += size
            self.assertEqual(offset, len(data))
            self.assertGreater(len(sizes), 1)

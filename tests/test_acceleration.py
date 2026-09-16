import json
import http.client
import io
import os
from pathlib import Path
import tempfile
import unittest
import threading
from unittest.mock import patch, Mock

from psp_streamer.acceleration import Acceleration, hardware_command
from psp_streamer.server import ffmpeg_command, AppServer, Library, MediaItem


class AccelerationTests(unittest.TestCase):
    def test_hardware_start_failure_retries_before_http_headers(self):
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ,
                {'PSP_STREAMER_SETTINGS_DIR':'','PSP_STREAMER_PASSWORD':'',
                 'PSP_STREAMER_TLS_CERT':'','PSP_STREAMER_TLS_KEY':'','PSP_STREAMER_ACCELERATION':'auto'}):
            root=Path(directory); (root/'test.mkv').touch()
            library=Library([root]); token=library.encode(MediaItem(0,'test.mkv'))
            for fallback in (True,False):
                with AppServer(('127.0.0.1',0),library) as server:
                    bad=Mock(stdout=io.BytesIO(b'')); bad.poll.return_value=1
                    good=Mock(stdout=io.BytesIO(b'FLV-software')); good.poll.return_value=0
                    with patch.object(server.acceleration,'prepare',return_value=(['hardware'],'vaapi',fallback)), \
                         patch('psp_streamer.server.subprocess.Popen',side_effect=[bad,good]) as popen:
                        thread=threading.Thread(target=server.serve_forever);thread.start()
                        connection=http.client.HTTPConnection(*server.server_address,timeout=3)
                        try:
                            connection.request('GET',f'/api/transcode/{token}?container=flv')
                            response=connection.getresponse(); body=response.read()
                            if fallback:
                                self.assertEqual(response.status,200)
                                self.assertEqual(body,b'FLV-software')
                                self.assertEqual(response.getheader('X-Video-Encoder'),'software')
                                self.assertEqual(popen.call_count,2)
                                self.assertIn('libx264',popen.call_args.args[0])
                            else:
                                self.assertEqual(response.status,503)
                                self.assertEqual(popen.call_count,1)
                                self.assertEqual(server.acceleration.status()['active'],'unavailable')
                        finally:
                            connection.close();server.shutdown();thread.join()

    def test_command_preserves_timing_audio_and_both_subtitle_filters(self):
        for tv in (False, True):
            for bitmap in (False, True):
                original = ffmpeg_command(Path('/media/movie.mkv'), 1, 'flv',
                                          subtitle_track=0, bitmap_subtitle=bitmap,
                                          tv_output=tv, start_seconds=30, video_fps='24000/1001')
                for backend in ('vaapi', 'nvenc'):
                    command = hardware_command(original, backend, '/dev/dri/renderD128')
                    self.assertIn('h264_'+backend, command)
                    self.assertNotIn('-x264-params', command)
                    self.assertNotIn('libx264', command)
                    for key in ('-af','-ss','-c:a','-b:a','-ar','-b:v','-maxrate','-bufsize'):
                        self.assertEqual(command[command.index(key)+1], original[original.index(key)+1])
                    self.assertEqual(command[command.index('-bf')+1], '0')
                    self.assertEqual(command[-1], 'pipe:1')
                    key = '-filter_complex' if bitmap else '-vf'
                    value = command[command.index(key)+1]
                    self.assertIn('fps=24000/1001', value)
                    if backend == 'vaapi':
                        self.assertIn('format=nv12,hwupload', value)
                        self.assertEqual(command[command.index('-level:v')+1], '30')
                        if bitmap: self.assertTrue(value.endswith('hwupload[v]'))

    def test_software_is_unchanged_and_audio_never_probed(self):
        with patch.dict(os.environ, {'PSP_STREAMER_SETTINGS_DIR':'','PSP_STREAMER_ACCELERATION':'software'}):
            acceleration = Acceleration()
            cmd = ffmpeg_command(Path('movie.mkv'),0,'flv')
            with patch.object(acceleration, 'probe', side_effect=AssertionError('unexpected probe')):
                self.assertEqual(acceleration.prepare(cmd), (cmd,'software',False))
                acceleration.mode='auto'
                audio=ffmpeg_command(Path('song.mp3'),0,'mp3')
                self.assertEqual(acceleration.prepare(audio), (audio,'software',False))

    def test_cache_for_each_layout_fallback_and_forced_failure(self):
        with patch.dict(os.environ, {'PSP_STREAMER_SETTINGS_DIR':'','PSP_STREAMER_ACCELERATION':'auto'}):
            a=Acceleration(); cmd=ffmpeg_command(Path('movie.mkv'),0,'flv')
            with patch.object(a,'probe',return_value=(True,'ok')) as probe:
                self.assertEqual(a.prepare(cmd)[1:],('vaapi',True))
                a.prepare(cmd); self.assertEqual(probe.call_count,1)
                a.prepare(cmd,True); self.assertEqual(probe.call_count,2)
                a.prepare(cmd,False,'24000/1001'); self.assertEqual(probe.call_count,3)
            a.cache.clear()
            with patch.object(a,'probe',return_value=(False,'no compatible GPU')):
                self.assertEqual(a.prepare(cmd),(cmd,'software',False))
                self.assertIn('no compatible GPU',a.status()['detail'])
                a.mode='vaapi'
                with self.assertRaises(ValueError): a.prepare(cmd)

    def test_persistent_settings_validation_and_environment_only(self):
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ,
                {'PSP_STREAMER_SETTINGS_DIR':directory,'PSP_STREAMER_ACCELERATION':'software'}):
            a=Acceleration(); a.change('auto','/dev/dri/renderD129')
            self.assertEqual(Acceleration().status()['device'],'/dev/dri/renderD129')
            for mode,device in [('bad','/dev/dri/renderD128'),('auto','/etc/passwd'),('auto',None),(None,'/dev/dri/renderD128')]:
                with self.assertRaises(ValueError): a.change(mode,device)
            self.assertEqual(a.mode,'auto')
            with patch.dict(os.environ, {'PSP_STREAMER_SETTINGS_DIR':''}):
                with self.assertRaises(ValueError): Acceleration().change('auto','/dev/dri/renderD128')

    def test_probe_checks_actual_encoded_stream(self):
        from types import SimpleNamespace
        good = {'streams':[{'codec_name':'h264','profile':'Constrained Baseline','level':30,
                           'has_b_frames':0,'pix_fmt':'yuv420p','width':480,'height':272}],
                'packets':[{'pts':i*50,'dts':i*50} for i in range(8)]}
        for profile, expected in [('Constrained Baseline',True),('High',False)]:
            good['streams'][0]['profile']=profile
            with patch('psp_streamer.acceleration.subprocess.run',side_effect=[
                    SimpleNamespace(returncode=0),SimpleNamespace(stdout=json.dumps(good))]):
                self.assertEqual(Acceleration.probe('vaapi','/dev/dri/renderD128',False,'20')[0],expected)

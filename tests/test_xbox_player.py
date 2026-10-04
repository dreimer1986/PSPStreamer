"""Focused Xbox codec/PTS, HTTP and cancellation checks; no console emulation."""
import http.client
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

from psp_streamer.server import AppServer, Library, MediaItem, ffmpeg_command
from psp_streamer.xbox_player import Transport, command, record


def unpack(data):
    result = []
    while data:
        kind, size, pts = struct.unpack('<c3xIq', data[:16])
        if len(data) < 16+size:
            raise ValueError('truncated record')
        result.append((kind, pts, data[16:16+size]))
        data = data[16+size:]
    return result


class XboxTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temp.name)
        cls.source = cls.root/'sample.mkv'
        subprocess.run(['ffmpeg','-v','error','-f','lavfi','-i','testsrc2=size=160x90:rate=24',
            '-f','lavfi','-i','sine=sample_rate=44100','-t','3','-c:v','libx264','-c:a','aac',str(cls.source)],check=True)
        subprocess.run(['cc','-O2','tests/xbox_decode.c','-lm','-o',str(cls.root/'decode')],check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def decode(self, data):
        path = self.root/'packets.xsm'
        path.write_bytes(data)
        result = subprocess.run([str(self.root/'decode'),str(path)],capture_output=True,check=True)
        expected = unpack(data)
        self.assertEqual(list(map(int,result.stdout.split())),
                         [sum(x[0]==b'V' for x in expected),sum(x[0]==b'A' for x in expected)])

    def test_pts_match_ffprobe_and_low_level_decoders(self):
        for audio_only in (False, True):
            base=ffmpeg_command(self.source,0,'mp3' if audio_only else 'flv',tv_output=True)
            cmd=command(base,audio_only)
            self.assertEqual(base[base.index('-c:a')+1],'libmp3lame')
            output=subprocess.run(cmd,capture_output=True,check=True).stdout
            transport=Transport();parts=[]
            for offset in range(0,len(output),997):parts.extend(transport.feed(output[offset:offset+997]))
            parts.extend(transport.finish())
            data=b''.join(parts);records=unpack(data)
            self.assertEqual(records[-1],(b'E',0,b''))
            ts=self.root/'result.ts';ts.write_bytes(output)
            probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-of','json',str(ts)]))
            for kind,codec in ((b'V','video'),(b'A','audio')):
                self.assertEqual([p[1] for p in records if p[0]==kind],
                                 [int(p['pts']) for p in probe['packets'] if p['codec_type']==codec])
            self.decode(data)

    def test_packet_bounds_and_partial_ts(self):
        with self.assertRaises(ValueError):record(b'V',0,b'x'*(262144+1))
        transport=Transport();list(transport.feed(b'x'))
        with self.assertRaises(ValueError):list(transport.finish())

    def test_short_audio_is_padded_only_until_video_end(self):
        source=self.root/'short-audio.mkv'
        subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','testsrc2=size=160x90:rate=24:duration=3',
            '-f','lavfi','-i','sine=sample_rate=48000:duration=1','-c:v','libx264','-c:a','aac',str(source)],check=True)
        data=subprocess.run(command(ffmpeg_command(source,0,'flv')),capture_output=True,check=True,timeout=15).stdout
        transport=Transport();packed=b''.join([*transport.feed(data),*transport.finish()])
        records=unpack(packed);a=[p[1] for p in records if p[0]==b'A'];v=[p[1] for p in records if p[0]==b'V']
        self.assertGreater(a[-1],250000);self.assertLess(abs(a[-1]-v[-1]),9000)
        self.decode(packed)

    def test_http_library_stream_auth_and_stop(self):
        with patch.dict(os.environ,{'PSP_STREAMER_SETTINGS_DIR':str(self.root/'settings'),'MAX_TRANSCODES':'1'}):
            server=AppServer(('127.0.0.1',0),Library([self.root]))
            thread=threading.Thread(target=server.serve_forever);thread.start()
            try:
                token=server.library.encode(MediaItem(0,self.source.name))
                con=http.client.HTTPConnection(*server.server_address,timeout=30)
                con.request('GET','/api/xbox/library?path=:files:/0');r=con.getresponse()
                self.assertEqual(r.status,200);listing=json.loads(r.read())
                self.assertTrue(any(x.get('id')==token for x in listing['entries']))
                con.request('GET','/api/xbox-stream/'+token+'?start=1');r=con.getresponse()
                self.assertEqual(r.status,200);data=r.read();self.assertEqual(data[:8],b'XSM1\3\0\0\0')
                self.decode(data[8:]);self.assertEqual(server.remote_sequence,0)
                con.request('GET','/api/xbox-stream/'+token);r=con.getresponse();r.read(4096);r.close();con.close()
                deadline=time.monotonic()+5
                while time.monotonic()<deadline:
                    if server.transcode_slots.acquire(blocking=False):server.transcode_slots.release();break
                    time.sleep(.05)
                else:self.fail('Xbox Stop did not release encoder')
                server.settings.bootstrap=b'test-password'
                con=http.client.HTTPConnection(*server.server_address,timeout=10)
                con.request('GET','/api/xbox/library');r=con.getresponse();self.assertEqual(r.status,401);r.read();con.close()
            finally:
                server.shutdown();thread.join();server.server_close()

if __name__=='__main__':unittest.main()

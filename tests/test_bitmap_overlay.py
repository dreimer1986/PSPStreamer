import io
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
from psp_streamer.pgs import PgsCue, lcd_cue
from psp_streamer.server import AppHandler
from psp_streamer.offline import OfflineQueue

ROOT=Path(__file__).resolve().parents[1]


class BitmapOverlayTests(unittest.TestCase):
    def test_full_canvas_and_exact_old_lcd_sampling(self):
        cue=PgsCue(91.091,96.013,0,0,1920,1079,(bytes(range(256))*8093)[:1920*1079],bytes(range(256))*4,1920,1080)
        small=lcd_cue(cue)
        self.assertEqual((small.width,small.height),(480,271))
        self.assertEqual(len(small.palette)+len(small.pixels),131104)
        self.assertEqual(small.palette,cue.palette)
        for y in range(small.height):
            for x in range(small.width):
                sx=min(cue.width-1,(x*1920+960)//480)
                sy=min(cue.height-1,(y*1080+540)//272)
                self.assertEqual(small.pixels[y*small.width+x],cue.pixels[sy*1920+sx])
        self.assertEqual((small.start,small.end),(cue.start,cue.end))

    def test_offsets_empty_and_bad_geometry(self):
        cue=PgsCue(0,1,800,900,400,100,bytes([7])*40000,bytes(1024),1920,1080)
        small=lcd_cue(cue)
        self.assertEqual((small.x,small.y,small.width,small.height),(200,226,100,25))
        self.assertEqual(set(small.pixels),{7})
        cue.x=2000;self.assertEqual(lcd_cue(cue).palette,bytes(1024))
        cue.canvas_height=0
        with self.assertRaises(ValueError):lcd_cue(cue)

    def test_metadata_and_sprite_agree_and_legacy_is_unchanged(self):
        raw=PgsCue(1,2,0,0,1920,1079,bytes(1920*1079),bytes(1024),1920,1080)
        small=lcd_cue(raw);handler=AppHandler.__new__(AppHandler)
        handler.pgs_cues=lambda token,track,lcd=False:[small if lcd else raw]
        replies=[];handler.send_json=lambda value:replies.append(value)
        handler.bitmap_subtitles('test',0,milliseconds=True,lcd=True)
        self.assertEqual(replies[-1]['c'],[[1000,2000,0,0,480,271,480,272]])
        handler.send_response=lambda _:None;headers={}
        handler.send_header=lambda k,v:headers.update({k:v});handler.end_headers=lambda:None
        handler.wfile=io.BytesIO();handler.bitmap_sprite('test',0,0,lcd=True)
        self.assertEqual(int(headers['Content-Length']),len(handler.wfile.getvalue()))
        self.assertEqual(len(handler.wfile.getvalue()),131104)
        handler.wfile=io.BytesIO();handler.bitmap_sprite('test',0,0)
        self.assertEqual(len(handler.wfile.getvalue()),2072704)

    def test_new_offline_package_scaled_tv_still_burns(self):
        raw=PgsCue(1,2,0,0,1920,1079,bytes(1920*1079),bytes(1024),1920,1080)
        with tempfile.TemporaryDirectory() as directory:
            folder=Path(directory);source=folder/'movie.mkv';source.touch()
            queue=OfflineQueue.__new__(OfflineQueue);queue.library=SimpleNamespace()
            def capture(command,*args):
                if command[0]=='mkvmerge':return b'{"tracks":[{"id":2,"type":"subtitles"}]}'
                (folder/'extract.sup').write_bytes(b'fixture');return b''
            queue._capture=capture
            job={'id':'test','subtitle':0,'profile':'normal'}
            probe={'streams':[{'codec_type':'subtitle','codec_name':'hdmv_pgs_subtitle'}]}
            with patch('psp_streamer.external_subtitles.payload',return_value=None),patch('psp_streamer.pgs.parse_pgs',return_value=[raw]):
                self.assertEqual(queue._subtitles(job,source,probe,folder),-1)
                data=(folder/'subtitles.ovl').read_bytes();length=struct.unpack('<I',data[4:8])[0]
                payload=json.loads(data[8:8+length]);self.assertEqual(payload['c'][0][4:],[480,271,480,272])
                self.assertEqual(len(data)-8-length,131104)
                job['profile']='tv';self.assertEqual(queue._subtitles(job,source,probe,folder),0)

    def test_actual_psp_worker(self):
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'bitmap'
            subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-pthread','-I',str(ROOT/'psp-client'),str(ROOT/'tests/bitmap_overlay_harness.c'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=10)

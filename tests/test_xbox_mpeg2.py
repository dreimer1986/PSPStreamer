"""Focused MPEG-1/2 adapter/EOF, Xbox profile and matrix-stereo tests."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from psp_streamer.server import ffmpeg_command
from psp_streamer.xbox_player import PROFILES, Transport, command


class MPEG2Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.tmp.name)
        cls.source = cls.root/'source.mkv'
        vendor = Path('xbox-client/vendor/libmpeg2')
        subprocess.run(['cc','-O3','-I'+str(vendor),'tests/xbox_mpeg2_decode.c',
                        *map(str,vendor.glob('*.c')),'-o',str(cls.root/'decode')],check=True)
        subprocess.run(['ffmpeg','-v','error','-f','lavfi','-i','testsrc2=size=320x240:rate=24',
                        '-f','lavfi','-i','sine=sample_rate=48000','-t','0.5',
                        '-c:v','libx264','-c:a','aac',str(cls.source)],check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def encode(self, codec, size, matrix='none'):
        base=ffmpeg_command(self.source,0,'flv',tv_output=True)
        cmd=command(base,False,size,codec,matrix)
        cmd.remove('-re');at=cmd.index('-readrate_initial_burst');del cmd[at:at+2]
        data=subprocess.check_output(cmd)
        transport=Transport()
        # Deliberately split across TS and PES boundaries.
        packed=b''.join(part for at in range(0,len(data),137) for part in transport.feed(data[at:at+137]))
        packed+=b''.join(transport.finish())
        return data,packed

    def test_profiles_pts_and_last_picture(self):
        for codec in ('mpeg1','mpeg2'):
            for size,(width,height,_) in PROFILES.items():
                with self.subTest(codec=codec,size=size):
                    data,packed=self.encode(codec,size)
                    path=self.root/'sample.xsm';path.write_bytes(packed)
                    result=subprocess.check_output([str(self.root/'decode'),str(path)])
                    self.assertEqual(int(result),12)
                    probe=subprocess.check_output(['ffprobe','-v','error','-select_streams','v:0',
                        '-show_entries','stream=width,height,codec_name','-of','csv=p=0','-i','pipe:0'],input=data).decode()
                    self.assertIn(f'{codec}video,{width},{height}',probe)
                    # A disconnect is not a clean end and must never autoplay.
                    path.write_bytes(packed[:-16])
                    self.assertEqual(subprocess.run([str(self.root/'decode'),str(path)]).returncode,7)

    def test_matrix_modes_and_profile_validation(self):
        for matrix in ('none','dolby','dplii'):
            data,_=self.encode('mpeg2','480p',matrix)
            self.assertGreater(len(data),0)
        for kwargs in ({'size':'9999p'},{'codec':'h264'},{'matrix':'invalid'}):
            with self.assertRaises(ValueError):command([],**kwargs)

    def test_surround_input_reaches_stereo_matrix(self):
        source=self.root/'surround.wav'
        subprocess.run(['ffmpeg','-v','error','-f','lavfi','-i',
            'aevalsrc=0|0|0|0|0.2*sin(2*PI*440*t)|0:s=48000:c=5.1',
            '-t','0.1',str(source)],check=True)
        outputs=[]
        for matrix in ('none','dolby','dplii'):
            cmd=command(ffmpeg_command(source,0,'mp3'),True,None,'mpeg2',matrix)
            data=subprocess.check_output(cmd)
            pcm=subprocess.check_output(['ffmpeg','-v','error','-i','pipe:0','-f','s16le','pipe:1'],input=data)
            self.assertGreater(len(pcm),0);outputs.append(pcm)
        self.assertTrue(outputs[0]!=outputs[1], 'Dolby matrix must differ from plain stereo')
        self.assertTrue(outputs[0]!=outputs[2], 'DPLII matrix must differ from plain stereo')

    def test_bitmap_filter_has_anamorphic_tail(self):
        base=ffmpeg_command(self.source,0,'flv',tv_output=True,subtitle_track=0,bitmap_subtitle=True)
        cmd=command(base,False,'480p','mpeg2')
        graph=cmd[cmd.index('-filter_complex')+1]
        self.assertIn(',scale=720:480,setsar=7680/6480[v]',graph)
        self.assertIn('overlay=eof_action=pass:repeatlast=0',graph)
        self.assertNotIn('scale=720:480,setsar=',base[base.index('-filter_complex')+1])

if __name__=='__main__':unittest.main()

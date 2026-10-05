import array
from pathlib import Path
import subprocess
import tempfile
import threading
import unittest
from psp_streamer.server import ffmpeg_command, _ffmpeg_command, Library, MediaItem, parse_srt_cues
from psp_streamer.offline import OfflineQueue

class PspSurroundTests(unittest.TestCase):
    def test_default_unchanged_and_timestamps(self):
        for container in ('mp3','flv','h264'):
            plain=_ffmpeg_command(Path('test.mkv'),0,container)
            self.assertEqual(ffmpeg_command(Path('test.mkv'),0,container),plain)
            for matrix in ('dolby','dplii'):
                result=ffmpeg_command(Path('test.mkv'),0,container,audio_matrix=matrix)
                if container=='h264':self.assertEqual(result,plain);continue
                self.assertEqual(result[result.index('-ar')+1],'44100')
                self.assertEqual(result[result.index('-c:a')+1],'libmp3lame')
                self.assertIn('matrix_encoding='+matrix,result[result.index('-af')+1])
                if container=='flv':self.assertIn('first_pts=0',result[result.index('-af')+1])
        with self.assertRaises(ValueError):ffmpeg_command(Path('test.mkv'),0,'mp3',audio_matrix='invalid')

    def test_real_surround_phase(self):
        for matrix in ('dolby','dplii'):
            command=ffmpeg_command(Path('unused'),0,'mp3',audio_matrix=matrix)
            data=subprocess.check_output(['ffmpeg','-v','error','-f','lavfi','-i',
                'aevalsrc=0|0|0|0|0.1*sin(2*PI*440*t)|0:s=48000:c=5.1',
                '-t','0.05','-af',command[command.index('-af')+1],'-ac','2','-f','f32le','pipe:1'])
            samples=array.array('f',data);left=samples[::2];right=samples[1::2]
            self.assertGreater(sum(v*v for v in left),0.01)
            self.assertLess(sum(a*b for a,b in zip(left,right)),0) # phase-encoded rear channel

    def test_offline_options_and_saved_default(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'test.mp3').touch();library=Library([root])
            queue=OfflineQueue(root/'cache',library,ffmpeg_command,parse_srt_cues,threading.Semaphore(1))
            token=library.encode(MediaItem(0,'test.mp3'))
            self.assertEqual(queue.prepare({'id':token})['audio_matrix'],'none')
            self.assertEqual(queue.prepare({'id':token,'audio_matrix':'dolby'})['audio_matrix'],'dolby')
            with self.assertRaises(ValueError):queue.prepare({'id':token,'audio_matrix':'bad'})
            queue.preferences({'audio_matrix':'dplii'})
            self.assertEqual(queue.preferences()['audio_matrix'],'dplii')

if __name__=='__main__':unittest.main()

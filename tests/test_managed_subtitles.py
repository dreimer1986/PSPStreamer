import unittest
from unittest.mock import Mock, patch
from psp_streamer.managed_subtitles import jellyfin_burnin, seek_timeline, _cache


class ManagedSubtitleTests(unittest.TestCase):
    def setUp(self):
        _cache.values.clear()
        self.provider=Mock()
        self.provider.config={'url':'https://example.invalid','user':'u','token':'secret'}
        self.provider.split.return_value=['1'*32]
        self.provider.request.return_value=b'[Script Info]\n'
        self.source={'Id':'2'*32,'MediaStreams':[
            {'Index':5,'Type':'Subtitle','Codec':'ass'},
            {'Index':7,'Type':'Subtitle','Codec':'pgssub'}]}

    def test_native_ass_and_cache(self):
        with patch('psp_streamer.managed_subtitles.selected',return_value=self.source):
            self.assertEqual(jellyfin_burnin(self.provider,'item',0),('ass',b'[Script Info]\n'))
            jellyfin_burnin(self.provider,'item',0)
            self.provider.request.assert_called_once_with('/Videos/'+('1'*32)+'/'+('2'*32)+'/Subtitles/5/Stream.ass',raw=True,timeout=120)
            self.assertIsNone(jellyfin_burnin(self.provider,'item',1))

    def test_seek_has_balanced_pts_and_ignores_bitmap(self):
        cmd=['ffmpeg','-vf',"subtitles='subtitle.ass':si=0,scale=720:480"]
        seek_timeline(cmd,120)
        self.assertTrue(cmd[2].startswith('setpts=PTS+120.000/TB,subtitles='))
        self.assertTrue(cmd[2].endswith(',setpts=PTS-120.000/TB'))
        bitmap=['ffmpeg','-filter_complex','[0:v][0:s]overlay[v]']
        self.assertEqual(seek_timeline(bitmap.copy(),120),bitmap)

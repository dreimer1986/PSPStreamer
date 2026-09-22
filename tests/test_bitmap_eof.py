"""Only the bitmap burn-in EOF contract; no unrelated playback tests."""
from pathlib import Path
import unittest

from psp_streamer.server import ffmpeg_command
from psp_streamer.plex_media import RemoteSource


class BitmapEofTests(unittest.TestCase):
    def test_bitmap_timeline_does_not_extend_or_shorten_main_video(self):
        for source in (Path('/media/episode.mkv'),
                       RemoteSource('http://127.0.0.1:1234/original', 'episode.mkv', 'key')):
            for container in ('flv', 'h264'):
                with self.subTest(source=type(source).__name__, container=container):
                    cmd = ffmpeg_command(source, 1, container, subtitle_track=1,
                                         bitmap_subtitle=True, tv_output=True)
                    graph = cmd[cmd.index('-filter_complex') + 1]
                    self.assertIn('[0:v:0][0:s:1]overlay=eof_action=pass:repeatlast=0,', graph)
                    self.assertNotIn('shortest', ' '.join(cmd))
                    self.assertIn('scale=720:480', graph)

    def test_no_bitmap_filter_for_client_side_or_text_subtitles(self):
        for track in (-1, 0):
            cmd = ffmpeg_command(Path('/media/episode.mkv'), 1, 'flv', subtitle_track=track)
            self.assertNotIn('-filter_complex', cmd)
            self.assertNotIn('overlay', ' '.join(cmd))

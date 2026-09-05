import tempfile
import unittest
from pathlib import Path

from psp_streamer.server import Library, MediaItem, ffmpeg_command, load_roots, natural_name_key, parse_srt_cues, psp_subtitle_text, track_label


class LibraryTests(unittest.TestCase):
    def test_browse_and_reject_escape(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "Film.mkv").touch()
            (root / "Serien").mkdir()
            library = Library([root])
            result = library.browse(0)
            self.assertEqual(result["videos"][0]["name"], "Film.mkv")
            self.assertEqual(result["folders"][0]["name"], "Serien")
            with self.assertRaises(ValueError):
                library.browse(0, "../")

    def test_token_resolves_only_a_video_in_root(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "Film.mp4").touch()
            library = Library([root])
            token = library.encode(MediaItem(0, "Film.mp4"))
            _, source = library.decode(token)
            self.assertEqual(source, root / "Film.mp4")

    def test_command_has_psp_constraints(self):
        command = ffmpeg_command(Path("/media/test.mkv"), 1)
        self.assertIn("baseline", command)
        self.assertIn("480:272", " ".join(command))
        self.assertIn("0:a:1?", command)

    def test_mjpeg_command_is_realtime_video_only(self):
        command = ffmpeg_command(Path("/media/test.mkv"), 0, "mjpeg")
        self.assertIn("-re", command)
        self.assertIn("mjpeg", command)
        self.assertIn("-an", command)
        self.assertIn("480:272", " ".join(command))

    def test_h264_command_has_access_unit_markers(self):
        command = ffmpeg_command(Path("/media/test.mkv"), 0, "h264")
        self.assertIn("baseline", command)
        self.assertIn("-re", command)
        self.assertIn("aud=1:repeat-headers=1:keyint=64", " ".join(command))
        self.assertIn("-an", command)
        self.assertIn("fps=201/10", " ".join(command))

    def test_h264_subtitle_filter_can_use_a_safe_alias(self):
        command = ffmpeg_command(
            Path("/media/An Archdemon's Dilemma.mkv"), 0, "h264",
            subtitle_track=1, subtitle_source=Path("/tmp/subtitle-source.mkv"),
        )
        filter_value = command[command.index("-vf") + 1]
        self.assertIn("/tmp/subtitle-source.mkv", filter_value)
        self.assertNotIn("Archdemon", filter_value)

    def test_mp3_command_is_dac_paced_audio_only(self):
        command = ffmpeg_command(Path("/media/test.mkv"), 0, "mp3")
        self.assertNotIn("-re", command)
        self.assertIn("44100", command)
        self.assertIn("libmp3lame", command)
        self.assertIn("160k", command)
        self.assertIn("-vn", command)
        self.assertIn("-write_xing", command)

    def test_low_profile_reduces_h264_rate_and_keeps_mp3_audio_rate(self):
        video = " ".join(ffmpeg_command(Path("/media/test.mkv"), 0, "h264", True))
        audio = " ".join(ffmpeg_command(Path("/media/test.mkv"), 0, "mp3", True))
        self.assertIn("-b:v 400k", video)
        self.assertIn("-maxrate 450k", video)
        self.assertIn("44100", audio)

    def test_load_roots_ignores_nonexistent_entries(self):
        with tempfile.TemporaryDirectory() as temporary:
            self.assertEqual(load_roots(f"/missing:{temporary}"), [Path(temporary).resolve()])

    def test_text_subtitles_are_compact_psp_frame_cues(self):
        cues = parse_srt_cues("""1
00:00:01,000 --> 00:00:02,500
Hallo <i>Welt</i>!\\N{\\an8}Oben
""")
        self.assertEqual(cues, [[20, 50, "Hallo Welt!|Oben"]])
        self.assertEqual(psp_subtitle_text("Grüße \"PSP\""), "Grüße 'PSP'")

    def test_track_labels_and_episode_sorting_are_psp_friendly(self):
        self.assertEqual(track_label('German "Forced"'), "German 'Forced'")
        self.assertLess(natural_name_key("Episode 2.mkv"), natural_name_key("Episode 10.mkv"))


if __name__ == "__main__":
    unittest.main()

import tempfile
import http.client
import json
import threading
import unittest
from pathlib import Path

from psp_streamer.server import AppServer, Library, MediaItem, ffmpeg_command, load_roots, natural_name_key, parse_srt_cues, psp_subtitle_text, track_label


class LibraryTests(unittest.TestCase):
    def test_successor_stays_in_folder_and_media_kind(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "library"
            root.mkdir()
            (root.parent / "outside.mkv").touch()
            season = root / "Season 1"
            season.mkdir()
            (root / "Season 2").mkdir()
            (root / "Season 2" / "Episode 11.mkv").touch()
            for name in ("Episode 1.mkv", "Episode 2.mp4", "Episode 10.mkv",
                         "Episode 3.mp3", "Episode 4.flac", ".Episode 5.mkv", "notes.txt"):
                (season / name).touch()
            (season / "Episode 6.mkv").mkdir()  # not a media file
            (season / "Episode 7.mkv").symlink_to(root.parent / "outside.mkv")
            library = Library([root])
            def token(name):
                return library.encode(MediaItem(0, "Season 1/" + name))
            current = token("Episode 1.mkv")
            second = library.next_media(current, shuffle=True)  # video never shuffles
            self.assertEqual(second, {"id": token("Episode 2.mp4"), "kind": "video"})
            third = library.next_media(second["id"])
            self.assertEqual(third["id"], token("Episode 10.mkv"))
            self.assertEqual(library.next_media(third["id"]), {})
            self.assertEqual(library.next_media(token("Episode 3.mp3")),
                             {"id": token("Episode 4.flac"), "kind": "audio"})
            self.assertEqual(library.next_media(token("Episode 4.flac")), {})
            self.assertEqual(library.next_media(token("Episode 4.flac"), shuffle=True)["id"],
                             token("Episode 3.mp3"))
            self.assertEqual(library.next_media("__psp_calibration_10s__"), {})
            with self.assertRaises(ValueError):
                library.next_media(library.encode(MediaItem(0, "../outside.mkv")))

    def test_successor_has_no_page_limit_and_single_song_cannot_shuffle(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for episode in range(1, 61):
                (root / f"Episode {episode}.mkv").touch()
            (root / "Only song.mp3").touch()
            library = Library([root])
            token = lambda name: library.encode(MediaItem(0, name))
            for episode in (24, 48, 59):
                self.assertEqual(library.next_media(token(f"Episode {episode}.mkv"))["id"],
                                 token(f"Episode {episode + 1}.mkv"))
            self.assertEqual(library.next_media(token("Episode 60.mkv")), {})
            self.assertEqual(library.next_media(token("Only song.mp3"), shuffle=True), {})

    def test_successor_http_is_read_only_and_independent_of_browse(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "E01.mkv").touch()
            (root / "E02.mkv").touch()
            library = Library([root])
            server = AppServer(("127.0.0.1", 0), library)
            command = server.set_remote_command({"action": "stop"})
            thread = threading.Thread(target=server.serve_forever)
            thread.start()
            connection = http.client.HTTPConnection(*server.server_address, timeout=5)
            try:
                first = library.encode(MediaItem(0, "E01.mkv"))
                second = library.encode(MediaItem(0, "E02.mkv"))
                connection.request("GET", "/api/media-next/" + first)
                response = connection.getresponse()
                self.assertEqual(response.status, 200)
                self.assertEqual(json.loads(response.read()), {"id": second, "kind": "video"})
                connection.request("GET", "/api/media-next/" + second)
                response = connection.getresponse()
                self.assertEqual(json.loads(response.read()), {})
                self.assertEqual(server.remote_after(0), command)  # Stop is not replaced by Play
            finally:
                connection.close()
                server.shutdown()
                thread.join()
                server.server_close()

    def test_browse_and_reject_escape(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "Film.mkv").touch()
            (root / "Serien").mkdir()
            library = Library([root])
            result = library.browse(0)
            self.assertIn("Film.mkv", [entry["name"] for entry in result["videos"]])
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

    def test_library_marks_audio_files_as_audio_media(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "Titel.mp3").touch()
            entry = next(item for item in Library([root]).browse(0)["videos"] if item["name"] == "Titel.mp3")
            self.assertEqual(entry["kind"], "audio")

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

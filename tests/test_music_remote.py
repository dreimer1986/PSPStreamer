import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class MusicRemoteTests(unittest.TestCase):
    def test_worker_commands_and_lifecycle(self):
        source = (ROOT / "psp-client/main.c").read_text()
        # Test with the client's actual compact-JSON parsing functions.
        value = source[source.index("static int json_value(", source.index("static int play_h264(")):]
        value = value[:value.index("static void parse_stream_tracks")]
        integer = source[source.index("static int json_integer(", source.index("static int play_h264(")):]
        integer = integer[:integer.index("/* Browser commands")]
        harness = (ROOT / "tests/music_remote_harness.c").read_text()
        harness = harness.replace("/* JSON_FUNCTIONS */", value + integer)
        worker = source[source.index("static int remote_control_thread(SceSize args, void *argp) {"):]
        worker = worker[:worker.index("\n}\n") + 3]
        harness = harness.replace("/* VIDEO_REMOTE_WORKER */", worker)
        with tempfile.TemporaryDirectory() as directory:
            c_file = Path(directory) / "remote.c"
            binary = Path(directory) / "remote"
            c_file.write_text(harness)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=undefined", "-I", str(ROOT / "psp-client"),
                            str(c_file), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_music_consumes_controls_and_joins_before_return(self):
        source = (ROOT / "psp-client/main.c").read_text()
        music = source[source.index("static int play_audio("):source.index("static int play_h264(")]
        self.assertIn("remote_result = music_remote_start();", music)
        for action in ("PAUSE", "RESUME", "STOP", "PLAY", "SEEK"):
            self.assertIn("MUSIC_REMOTE_" + action, music)
        self.assertIn("stream_start_seconds = seek_seconds;", music)
        self.assertIn("resume_pending = seek_requested = 1;", music)
        self.assertIn("stopped_by_user = 1;", music)
        self.assertIn("music_remote_stop();", music)
        self.assertNotIn("http_get", music)
        self.assertIn("stream_start_seconds + (float)audio_played_blocks", music)
        idle = source[source.index("static int remote_poll_play("):source.index("static int remote_control_thread(SceSize args, void *argp) {")]
        self.assertNotIn("static int sequence", idle)
        self.assertIn("int sequence = remote_control_sequence;", idle)
        video = source[source.index("static int play_h264("):source.index("/* This is intentionally a narrow parser")]
        self.assertIn("action == 3 || action == 4", video)
        for cleanup in ("sceKernelWaitThreadEnd(timed_reader_id", "sceKernelWaitThreadEnd(audio_thread_id",
                        "sceKernelWaitThreadEnd(audio_output_thread_id", "h264_hw_shutdown();"):
            self.assertIn(cleanup, video)


if __name__ == "__main__":
    unittest.main()

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class PlaybackRecoveryTests(unittest.TestCase):
    def test_transport_and_recovery_state(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = str(Path(directory) / "recovery")
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=undefined", "-I", str(ROOT / "psp-client"),
                            str(ROOT / "tests/playback_recovery_harness.c"),
                            "-o", binary], check=True)
            subprocess.run([binary], check=True, timeout=5)

    def test_recovery_is_scoped_and_joins_before_retry(self):
        source = (ROOT / "psp-client/main.c").read_text()
        audio = source[source.index("static int audio_thread("):source.index('#include "music_ui.h"')]
        self.assertNotIn("connection_recv(", audio)
        self.assertIn("180000000ULL", audio)  # Keep cold-start preparation budget.
        self.assertIn("audio_state=-28", audio)
        self.assertNotIn("gethostbyname(server_host)", source)
        wrappers = source[source.index("static int comfort_play_audio("):source.index("/* This is intentionally a narrow parser")]
        self.assertIn("offline_active || radio_is_live(id) || !music_network_failed", wrappers)
        self.assertIn("result!=-1320 || !timed_network_failed", wrappers)
        self.assertIn("stream_start_seconds=playback_recovery_position(playback_position_ms)", wrappers)


if __name__ == "__main__":
    unittest.main()

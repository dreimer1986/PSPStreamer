"""Focused host tests; real game addresses/controllers require the PSP test."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class HealthRumbleTests(unittest.TestCase):
    def test_monitor_and_gui(self):
        with tempfile.TemporaryDirectory() as temp:
            for name in ("health_rumble_test", "health_rumble_ui_test", "title_rules_test"):
                binary = Path(temp) / name
                subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                                "-fsanitize=undefined", str(ROOT / "tests" / f"{name}.c"),
                                "-o", str(binary)], check=True)
                subprocess.run([str(binary)], check=True, timeout=5)

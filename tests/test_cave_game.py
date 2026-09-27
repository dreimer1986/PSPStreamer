"""Focused flight rules and score persistence; no preset collection tests."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class CaveGameTests(unittest.TestCase):
    def test_rules_and_scores(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "flight"
            subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=undefined", "-I", str(ROOT / "psp-client"),
                            str(ROOT / "tests/cave_game_harness.c"), "-lm", "-o", str(binary)], check=True)
            subprocess.run([str(binary), str(Path(tmp)/"a.dat"), str(Path(tmp)/"b.dat")], check=True, timeout=5)

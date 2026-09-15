"""Prevent Docker and Home Assistant copies from silently diverging."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DeploymentParityTests(unittest.TestCase):
    def test_source_and_web_assets_match(self):
        for directory in ("psp_streamer", "static"):
            original = ROOT / directory
            mirrored = ROOT / "psp_streamer_addon/rootfs/app" / directory
            def files(base):
                return {p.relative_to(base) for p in base.rglob("*")
                        if p.is_file() and "__pycache__" not in p.parts}
            self.assertEqual(files(original), files(mirrored))
            for path in files(original):
                with self.subTest(path=str(path)):
                    self.assertEqual((original / path).read_bytes(),
                                     (mirrored / path).read_bytes())

    def test_media_dependencies_present_in_both_images(self):
        for path in ("Dockerfile", "psp_streamer_addon/Dockerfile"):
            text = (ROOT / path).read_text()
            for dependency in ("ffmpeg", "fontconfig", "mkvtoolnix"):
                self.assertIn(dependency, text, path)

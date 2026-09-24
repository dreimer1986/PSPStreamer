"""Prevent Docker and Home Assistant copies from silently diverging."""
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DeploymentParityTests(unittest.TestCase):
    def test_home_assistant_artwork(self):
        app = ROOT / "psp_streamer_addon"
        self.assertTrue((app / "config.yaml").is_file())
        for name in ("icon.png", "logo.png"):
            data = (app / name).read_bytes()
            self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
            self.assertEqual(data[12:16], b"IHDR")
            width, height = struct.unpack(">II", data[16:24])
            self.assertGreater(width, 0)
            self.assertGreater(height, 0)
            self.assertLess(len(data), 65536)
            if name == "icon.png":
                self.assertEqual((width, height), (128, 128))
        self.assertEqual((app / "logo.png").read_bytes(),
                         (ROOT / "psp-client/assets/icon0.png").read_bytes())

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

    def test_dlna_deployments_use_host_network_and_persistent_state(self):
        addon = (ROOT / "psp_streamer_addon/config.yaml").read_text()
        self.assertIn("host_network: true", addon)
        self.assertNotIn("\nports:", addon)
        self.assertIn("PSP_STREAMER_STATE_DIR=/data",
                      (ROOT / "psp_streamer_addon/run.sh").read_text())
        compose = (ROOT / "compose.dlna.yaml").read_text()
        self.assertIn("network_mode: host", compose)
        self.assertIn("streamer-settings:/data", compose)
        self.assertIn("PSP_STREAMER_SETTINGS_DIR: /data", compose)

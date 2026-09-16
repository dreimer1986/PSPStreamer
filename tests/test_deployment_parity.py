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

    def test_hardware_encoding_is_packaged_for_both_deployments(self):
        docker = (ROOT / "Dockerfile").read_text()
        addon = (ROOT / "psp_streamer_addon/Dockerfile").read_text()
        self.assertIn("intel-media-va-driver-non-free", docker)
        self.assertIn("mesa-va-drivers", docker)
        self.assertIn("intel-media-driver", addon)
        self.assertIn("mesa-va-gallium", addon)
        self.assertIn("video: true", (ROOT / "psp_streamer_addon/config.yaml").read_text())
        for setting in ("PSP_STREAMER_ACCELERATION", "PSP_STREAMER_VAAPI_DEVICE"):
            self.assertIn(setting, (ROOT / "compose.yaml").read_text())
            self.assertIn(setting, (ROOT / "psp_streamer_addon/run.sh").read_text())
        self.assertIn("/dev/dri:/dev/dri", (ROOT / "compose.vaapi.yaml").read_text())
        self.assertIn("capabilities: [gpu]", (ROOT / "compose.nvidia.yaml").read_text())

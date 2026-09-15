import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

class StreamDiagnosticTests(unittest.TestCase):
    def test_stream_errors_and_native_fullscreen_bounds(self):
        for name in ("stream_diagnostic", "spectrum_fullscreen", "video_controls"):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                code=(ROOT / f"tests/{name}_harness.c").read_text()
                if name=="stream_diagnostic":
                    source=(ROOT / "psp-client/timed_stream.h").read_text()
                    code=code.replace("/* TIMED_READ */",source[source.index("static int timed_read("):source.index("static int timed_connect(")])
                path=Path(temp)/"test.c"; binary=Path(temp)/"test"
                path.write_text(code)
                subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror","-fsanitize=undefined",
                                "-I",str(ROOT / "psp-client"),str(path),"-o",str(binary)],check=True)
                subprocess.run([str(binary)],check=True,timeout=5)

from pathlib import Path
import subprocess
import re
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class AvccPacketTests(unittest.TestCase):
    def test_actual_queue_and_decoder_contract(self):
        queue = (ROOT/'psp-client/timed_stream.h').read_text()
        hardware = (ROOT/'psp-client/h264_hw.c').read_text()
        hardware = re.sub(r'#include <psp\w+\.h>\n', '', hardware)
        source = (ROOT/'tests/avcc_pipeline_harness.c').read_text()
        source = source.replace('/* QUEUE_TYPES */', queue[queue.index('typedef struct'):queue.index('static TimedQueue')])
        source = source.replace('/* QUEUE_FUNCTIONS */', queue[queue.index('static int timed_wait('):queue.index('static int timed_put_video(')])
        source = source.replace('/* VIDEO_PUT */', queue[queue.index('static int timed_put_video('):queue.index('/* Exact reads')])
        source = source.replace('/* HARDWARE_DECODER */', hardware)
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'pipeline.c';binary=Path(directory)/'pipeline'
            path.write_text(source)
            subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function',
                            '-fsanitize=address,undefined','-I',str(ROOT/'psp-client'),str(path),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=10)

    def test_packet_bounds_and_ownership(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'avcc'
            subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I', str(ROOT/'psp-client'),
                            str(ROOT/'tests/avcc_packet_harness.c'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)

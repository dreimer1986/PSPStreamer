from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class GamepadLearnTests(unittest.TestCase):
    def test_actual_profile_commands(self):
        source=(ROOT/'streammaster/main/gamepad.c').read_text()
        types=source[source.index('typedef struct {uint8_t address[6],valid,reserved;SmBtProfile profile;}'):source.index('static uint8_t bonded')]
        command=source[source.index('int sm_gamepad_command('):source.index('static void gap(')]
        harness=(ROOT/'tests/streammaster_gamepad_profiles_harness.c').read_text().replace('/* PROFILE_TYPES */',types).replace('/* COMMAND */',command)
        with tempfile.TemporaryDirectory() as folder:
            binary=Path(folder)/'profiles'
            subprocess.run(['cc','-x','c','-','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                            '-I',str(ROOT),'-o',str(binary)],input=harness,text=True,check=True)
            subprocess.run([str(binary)],check=True,timeout=5)

    def test_capture_and_profiles(self):
        with tempfile.TemporaryDirectory() as folder:
            binary=Path(folder)/'learn'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                            '-I',str(ROOT),str(ROOT/'tests/streammaster_gamepad_learn_harness.c'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=5)

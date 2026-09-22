import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]


class PreparedTests(unittest.TestCase):
    def test_reference_equivalence_budget_yields_and_exact_memoization(self):
        with tempfile.TemporaryDirectory() as folder:
            outputs=[]
            for reference in (False,True):
                binary=Path(folder)/str(reference)
                subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror',
                    '-D__PSP__','-fsanitize=undefined,float-cast-overflow',
                    *(['-DPM_REFERENCE_EXECUTION'] if reference else []),
                    '-I',str(ROOT/'tests/preset_host'),'-I',str(ROOT/'psp-client'),
                    str(ROOT/'tests/preset_prepared_harness.c'),'-lm','-o',str(binary)],check=True)
                outputs.append(subprocess.check_output([str(binary)],timeout=15))
            self.assertEqual(outputs[0],outputs[1])

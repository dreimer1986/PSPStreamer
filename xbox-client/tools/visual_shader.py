"""Run nxdk's fragment compiler, then make its register-mask shifts unsigned."""
from pathlib import Path
import subprocess
import sys
import tempfile

cgc, compiler, source, output = sys.argv[1:]
with tempfile.TemporaryDirectory() as directory:
    intermediate = Path(directory) / 'program.fp20'
    subprocess.run([cgc, '-profile', 'fp20', '-o', str(intermediate), source], check=True)
    result = subprocess.check_output([compiler, str(intermediate)], text=True)
result = result.replace('((val) <<', '((uint32_t)(val) <<')
Path(output).write_text(result)

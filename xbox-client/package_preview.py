"""Complete the existing bin/ preview in place, without another release copy."""
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parent
sdk = root.parent / '.toolchain/nxdk'
out = root / 'bin'
if not (out / 'default.xbe').is_file():
    raise SystemExit('Build the XBE first')
for name in ('README.md', 'server.cfg.example'):
    shutil.copy2(root / name, out / name)
(out / 'source').mkdir(exist_ok=True)
for name in ('main.c', 'Makefile', 'server.cfg.example', 'README.md', 'package_preview.py'):
    shutil.copy2(root / name, out / 'source' / name)
licenses = out / 'licenses'
licenses.mkdir(exist_ok=True)
for source, name in (
    (root.parent / 'LICENSE', 'GPL-2.0.txt'),
    (root.parent / 'NOTICE', 'PSPStreamer-NOTICE.txt'),
    (sdk / 'LICENSES/MIT.txt', 'nxdk-MIT.txt'),
    (sdk / 'LICENSES/CC0-1.0.txt', 'nxdk-CC0.txt'),
    (sdk / 'LICENSES/NCSA.txt', 'compiler-rt-NCSA.txt'),
    (sdk / 'lib/pdclib/COPYING.CC0', 'PDCLib-CC0.txt'),
    (sdk / 'lib/net/lwip/COPYING', 'lwIP.txt'),
    (sdk / 'lib/sdl/SDL2/COPYING.txt', 'SDL2.txt'),
    (sdk / 'lib/usb/libusbohci/LICENSE.txt', 'USB-Apache-2.0.txt'),
    (sdk / 'lib/usb/libusbohci/DISCLAIMER.md', 'USB-DISCLAIMER.md'),
):
    shutil.copy2(source, licenses / name)
# Preserve SPDX copyright/license notices with the small nxdk glue sources.
for folder in ('hal', 'nxdk', 'xboxrt', 'winapi', 'usb', 'net'):
    for source in (sdk / 'lib' / folder).rglob('*'):
        if source.is_file() and source.suffix in ('.c', '.cpp', '.h', '.S') and not any(
                excluded in source.relative_to(sdk / 'lib').parts for excluded in ('lwip', 'libusbohci')):
            target = out / 'source/nxdk-notices' / source.relative_to(sdk / 'lib')
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
revision = subprocess.check_output(['git', '-C', str(sdk), 'rev-parse', 'HEAD'], text=True).strip()
(out / 'SDK-REVISION.txt').write_text('https://github.com/XboxDev/nxdk\n' + revision + '\n', encoding='utf-8')
print(out)

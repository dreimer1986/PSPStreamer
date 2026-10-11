#!/usr/bin/env python3
"""Package only the isolated FTP experiment; never replace standard releases."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('release', type=Path)
    args = parser.parse_args()
    out = args.release / 'Experimental' / 'PSPStreamFTP'
    files = {
        'PSPStreamFTP/PSPStreamFTP.prx': ROOT / 'psp-ftp/PSPStreamFTP.prx',
        'PSPStreamFTP/PSPStreamFTP.ini': ROOT / 'psp-ftp/PSPStreamFTP.ini.example',
        'README.md': ROOT / 'psp-ftp/README.md',
        'LICENSE': ROOT / 'LICENSE',
        'GPL-3.0.txt': ROOT / 'streammaster/GPL-3.0.txt',
        'BLUETOOTH.md': ROOT / 'streammaster/BLUETOOTH.md',
        'PSPLINK-license.txt': ROOT / 'streammaster/PSPLINK-license.txt',
        'streammaster-onju-v3-ftp-test.bin': ROOT / 'build-bluetooth-ftp/streammaster_onju_v3.bin',
    }
    idf = Path(os.environ.get('IDF_PATH', ROOT / '.toolchain/esp-idf'))
    for name, relative in {
        'ESP-IDF': 'LICENSE',
        'lwip': 'components/lwip/lwip/COPYING',
        'FreeRTOS': 'components/freertos/FreeRTOS-Kernel/LICENSE.md',
        'mbedTLS': 'components/mbedtls/mbedtls/LICENSE',
        'esp-wifi': 'components/esp_wifi/lib/LICENSE',
        'esp-phy': 'components/esp_phy/lib/LICENSE',
        'wpa-supplicant': 'components/wpa_supplicant/COPYING',
        'bt-tinycrypt': 'components/bt/common/tinycrypt/LICENSE',
    }.items():
        files[f'licenses/{name}.txt'] = idf / relative
    for source in files.values():
        if not source.is_file():
            raise SystemExit(f'Missing build input: {source}')
    # Validate app descriptor/version, guarding against accidentally packaging
    # the normal firmware under an experimental filename.
    firmware = files['streammaster-onju-v3-ftp-test.bin'].read_bytes()
    if b'0.3.25-bt-qio80-iram-ftp1\0' not in firmware:
        raise SystemExit('Expected FTP test firmware version not found')
    for name, source in files.items():
        target = out / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    dirty = subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True)
    (out / 'BUILD.txt').write_text(f'Source: {revision}{" (working tree changes)" if dirty else ""}\n'
                                 'Experimental FTP 0.3 / firmware 0.3.25-bt-qio80-iram-ftp1\n'
                                 'Hardware validation pending. Standard releases untouched.\n')
    names = sorted([*files, 'BUILD.txt'])
    (out / 'SHA256SUMS').write_text(''.join(
        f'{hashlib.sha256((out / name).read_bytes()).hexdigest()}  {name}\n' for name in names))
    archive = out.with_suffix('.zip')
    temporary = archive.with_suffix('.zip.tmp')
    with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED) as bundle:
        for name in [*names, 'SHA256SUMS']:
            bundle.write(out / name, f'PSPStreamFTP/{name}')
    temporary.replace(archive)
    print(archive)

if __name__ == '__main__':
    main()

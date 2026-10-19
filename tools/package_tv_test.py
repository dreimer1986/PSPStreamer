#!/usr/bin/env python3
"""Create a separate CRT reporter ZIP; never replace stable releases/configs."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seed', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    files = {}

    def add(source, destination):
        source = Path(source)
        if not source.is_file() or not source.stat().st_size:
            raise SystemExit(f'Missing input: {source}')
        files[destination] = source.read_bytes()

    app = 'PSP/GAME/PSPStreamer/'
    for name in ('EBOOT.PBP', 'PSPStreamer.prx', 'PSPStreamer.cfg.example'):
        add(ROOT / 'psp-client' / name, app + name)
    pbp = files[app + 'EBOOT.PBP']
    if len(pbp) < 40 or pbp[:4] != b'\0PBP':
        raise SystemExit('Invalid EBOOT.PBP')
    start, end = struct.unpack_from('<2I', pbp, 32)
    if pbp[start:end] != files[app + 'PSPStreamer.prx']:
        raise SystemExit('EBOOT.PBP and PSPStreamer.prx do not match')
    for name in ('cooleyesBridge.prx', 'dvemgr.prx', 'StreamMasterUSB.prx',
                 'MilkDrop2-license.txt', 'PNG-zlib-license.txt', 'PSPLINK-license.txt'):
        add(args.seed / 'PSPStreamer' / name, app + name)
    add(ROOT / 'psp-client/assets/subtitle_font.raw', app + 'subtitle_font.raw')
    add(ROOT / 'psp-client/TVOUT.md', app + 'TVOUT.md')
    for folder in ('presets', 'monkey'):
        for path in sorted((args.seed / 'PSPStreamer' / folder).rglob('*')):
            if path.is_file():
                add(path, app + path.relative_to(args.seed / 'PSPStreamer').as_posix())
    for name in ('AstroShield-CREDITS.md', 'Flight-models-CREDITS.md', 'cave_ship.CREDITS.md'):
        add(args.seed / 'PSPStreamer' / name, app + name)
    sources = {
        'PSPConsolizer': [
            'psp-controller/PSPConsolizer.prx', 'psp-controller/bridge/PSPConsolizerUSB.prx',
            'psp-controller/PSPConsolizer.ini.example', 'psp-controller/PSPConsolizer-rules.ini.example',
            'psp-controller/PSPConsolizer-rumble.ini.example', 'psp-controller/README.md',
            'psp-controller/RUMBLE.md', 'psp-controller/assets/xmb-background.xba'],
        'StreamerOC': ['psp-overclock/StreamerOC.prx', 'psp-overclock/StreamerOC.ini.example',
                       'psp-overclock/StreamerOC-rules.ini.example', 'psp-overclock/README.md'],
        'FuSaFullscreen': ['psp-fusa-probe/FuSaFullscreen.prx',
                           'psp-fusa-probe/PLUGINS-fullscreen-example.TXT'],
    }
    for component, paths in sources.items():
        base = f'SEPLUGINS/{component}/'
        for path in paths:
            add(ROOT / path, base + Path(path).name)
        add(ROOT / 'LICENSE', base + 'LICENSE')
    add(ROOT / 'LICENSE', app + 'LICENSE')
    add(ROOT / 'psp-fusa-probe/FuSaFullscreen.ini', 'SEPLUGINS/FuSaFullscreen/FuSaFullscreen.ini.example')
    add(ROOT / 'psp-fusa-probe/FULLSCREEN.md', 'SEPLUGINS/FuSaFullscreen/README.md')
    add(args.seed / 'PSPStreamer/dvemgr.prx', 'SEPLUGINS/FuSaFullscreen/dvemgr.prx')
    add(ROOT / 'docs/TV_CABLE_TEST.md', 'README-TEST.md')
    add(ROOT / 'CHANGELOG.md', 'CHANGELOG.md')
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True))
    manifest = {'revision': revision, 'dirty': dirty, 'hardware_validation': 'pending',
                'firmware_update_required': False,
                'files': {key: hashlib.sha256(value).hexdigest() for key, value in sorted(files.items())}}
    files['MANIFEST.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix('.zip.partial')
    with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, content in sorted(files.items()):
            archive.writestr(name, content)
    with zipfile.ZipFile(temporary) as archive:
        if archive.testzip():
            raise SystemExit('ZIP integrity failure')
    temporary.replace(args.output)
    print(args.output)
    print(hashlib.sha256(args.output.read_bytes()).hexdigest())


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Package an already built Xbox preview with the canonical PSP preset set."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def archive(folder, output):
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as z:
        for path in sorted(folder.rglob('*')):
            if path.is_file():
                z.write(path, path.relative_to(folder.parent))
    with zipfile.ZipFile(output) as z:
        if z.testzip():
            raise ValueError('Archive integrity check failed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('release', type=Path)
    args = parser.parse_args()
    release = args.release.resolve()
    build = ROOT / 'xbox-client/bin'
    known = release / 'PSPStreamer/presets'
    output = release / 'PSPStreamerXbox'
    if output.exists():
        raise SystemExit('Output already exists; preserve/review it before repackaging.')
    if not known.is_dir() or not (build / 'default.xbe').is_file():
        raise SystemExit('Build the Xbox player and supply the canonical PSP release first.')
    output.mkdir()
    for name in ('default.xbe', 'README.md', 'SDK-REVISION.txt', 'font.ttf',
                 'theme.png', 'visual-font.raw', 'server.cfg.example'):
        shutil.copy2(build / name, output / name)
    for name in ('licenses', 'monkey', 'presets'):
        shutil.copytree(build / name, output / name)
    # Keep host-readable sources in a single FATX-safe file, not long on-console paths.
    archive(build / 'source', output / 'source.zip')
    target = output / 'presets/Known'
    target.mkdir()
    mapping = {}
    for source in sorted(known.glob('*.milk')):
        stem = re.sub(r'[^A-Za-z0-9 _().-]', '_', source.stem).strip(' .') or 'Preset'
        suffix = hashlib.sha256(source.name.encode()).hexdigest()[:8]
        name = stem[:28].rstrip(' .') + '-' + suffix + '.milk'
        if name.casefold() in {n.casefold() for n in mapping}:
            raise ValueError('Preset name collision')
        shutil.copy2(source, target / name)
        assert digest(source) == digest(target / name)
        mapping[name] = source.name
    (target / 'names.json').write_text(json.dumps(mapping, ensure_ascii=True, indent=2) + '\n')
    (target / 'README.txt').write_text(
        'Familiar PSP release presets. Contents unchanged.\n'
        'FATX-safe filenames: see names.json for original names.\n'
        'Open Known in the MilkDrop preset browser.\n'
        'Performance and shader fallbacks depend on the preset.\n')
    shutil.copy2(ROOT / 'docs/RELEASE_2.4.md', output / 'CHANGELOG.md')
    for path in output.rglob('*'):
        for part in path.relative_to(output).parts:
            if len(part.encode('ascii')) > 42 or re.search(r'["*+,/:;<=>?\\|]', part):
                raise ValueError('Not FATX safe: ' + str(path))
    paths = sorted(p for p in output.rglob('*') if p.is_file())
    (output / 'SHA256SUMS').write_text(''.join(
        f'{digest(p)}  {p.relative_to(output).as_posix()}\n' for p in paths))
    archive(output, release / 'PSPStreamerXbox.zip')
    shutil.copy2(ROOT / 'docs/RELEASE_2.4.md', release / 'RELEASE-2.4.md')
    title = (ROOT / 'docs/RELEASE_2.4.md').read_text().splitlines()[0].removeprefix('# ')
    (release / 'RELEASE-TITLE.txt').write_text(title + '\n')
    manifest_path = release / 'MANIFEST.json'
    manifest = json.loads(manifest_path.read_text())
    manifest.setdefault('archives', {})['PSPStreamerXbox'] = digest(release / 'PSPStreamerXbox.zip')
    manifest.setdefault('component_revisions', {})['PSPStreamerXbox'] = subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'{release / "PSPStreamerXbox.zip"}: {len(mapping)} familiar presets; '
          f'{len(list((output / "presets").rglob("*.milk")))} presets total')
    print('XBE SHA256:', digest(output / 'default.xbe'))


if __name__ == '__main__':
    main()

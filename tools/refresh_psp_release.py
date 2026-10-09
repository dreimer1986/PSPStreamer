#!/usr/bin/env python3
"""Refresh only the PSP app and release notes, preserving other components/configs."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('release', type=Path)
    args = ap.parse_args()
    release = args.release.resolve(); app = release/'PSPStreamer'
    manifest_path = release/'MANIFEST.json'
    if not app.is_dir() or not manifest_path.is_file():
        raise SystemExit('Expected an existing canonical release with PSPStreamer/ and MANIFEST.json')
    manifest = json.loads(manifest_path.read_text())
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    for name in ('EBOOT.PBP', 'PSPStreamer.prx'):
        shutil.copy2(ROOT/'psp-client'/name, app/name)
    shutil.copy2(ROOT/'docs/PSP_PACKAGE_README.md', app/'README.md')
    (app/'SOURCE_REVISION.txt').write_text(revision+'\n')
    files = sorted(p for p in app.rglob('*') if p.is_file() and p.name != 'SHA256SUMS')
    (app/'SHA256SUMS').write_text(''.join(f'{digest(p)}  {p.relative_to(app).as_posix()}\n' for p in files))
    archive = release/'PSPStreamer.zip'
    staging = release/'PSPStreamer.zip.partial'
    with zipfile.ZipFile(staging, 'w', zipfile.ZIP_DEFLATED) as z:
        for p in sorted(app.rglob('*')):
            if p.is_file(): z.write(p, p.relative_to(release))
    with zipfile.ZipFile(staging) as z:
        if z.testzip(): raise RuntimeError('ZIP integrity failure')
    staging.replace(archive)
    for source, target in (('docs/RELEASE_LAYOUT.md', 'README.md'),
                           ('docs/RELEASE_2.5.md', 'RELEASE-2.5.md'),
                           ('CHANGELOG.md', 'CHANGELOG.md')):
        shutil.copy2(ROOT/source, release/target)
    title = (ROOT/'docs/RELEASE_2.5.md').read_text().splitlines()[0].removeprefix('# ')
    (release/'RELEASE-TITLE.txt').write_text(title+'\n')
    manifest['archives']['PSPStreamer'] = digest(archive)
    manifest.setdefault('component_revisions', {})['PSPStreamer'] = revision
    manifest['last_update_revision'] = revision
    manifest['release'] = '2.5'
    manifest['xmb_hardware_validation'] = 'pending'
    # Firmware/other-component provenance stays unchanged, as do their ZIPs.
    temporary = manifest_path.with_suffix('.json.partial')
    temporary.write_text(json.dumps(manifest, indent=2)+'\n')
    temporary.replace(manifest_path)
    print(archive)
    print('SHA256', digest(archive))


if __name__ == '__main__': main()

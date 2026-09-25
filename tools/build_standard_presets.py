#!/usr/bin/env python3
"""Curate a bounded, shaderless pack from a user's own preset collection.

Source files are never edited. Static cost is a selection heuristic, NOT FPS.
Keep third-party files local unless you have redistribution permission.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil


def candidate(path):
    raw = path.read_bytes()
    text = raw.decode('utf-8-sig', errors='replace')
    fields = dict((k.strip().lower(), v.strip()) for k, v in
                  re.findall(r'^([^;\s][^=\r\n]*)=(.*)$', text, re.M))
    def number(key, default=0):
        try: return float(fields.get(key, default))
        except ValueError: return default
    # fShader is MilkDrop 1 fixed-function colour shading, not HLSL.
    if any(k.startswith(('warp_', 'comp_')) and v.strip('` ') for k, v in fields.items()): return None
    if any(number(k) > 0 for k in ('ps_version', 'ps_version_warp', 'ps_version_comp')): return None
    formulas = {k: v.split('//')[0] for k, v in fields.items() if 'per_' in k}
    if any(re.search(r'\b(?:loop|while|megabuf|gmegabuf|memcpy|memset)\s*\(', v, re.I) for v in formulas.values()): return None
    if any('texture' in k and ('.jpg' in v.lower() or '.png' in v.lower()) for k, v in fields.items()): return None
    pixel = sum(len(v) for k, v in formulas.items() if k.startswith('per_pixel'))
    frame = sum(len(v) for k, v in formulas.items() if k.startswith('per_frame'))
    cost = frame + pixel * 289 + len(raw)//4
    for i in range(4):
        if number(f'wavecode_{i}_enabled'):
            samples = number(f'wavecode_{i}_samples', 512)
            if samples > 256: return None
            point = sum(len(v) for k, v in formulas.items() if k.startswith(f'wave_{i}_per_point'))
            cost += int(samples * (point + 12))
        if number(f'shapecode_{i}_enabled'):
            if number(f'shapecode_{i}_textured') or number(f'shapecode_{i}_num_inst', 1) > 4: return None
            cost += int(number(f'shapecode_{i}_sides', 4)*number(f'shapecode_{i}_num_inst', 1)*16)
    if cost > 45000 or frame > 8000: return None
    return {'name': path.name, 'sha256': hashlib.sha256(raw).hexdigest(), 'static_cost': cost,
            'changes': 'None; original bytes retained.'}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('collection', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--limit', type=int, default=128)
    args = p.parse_args()
    if not 1 <= args.limit <= 128: p.error('limit must be 1..128')
    if args.output.exists() and any(args.output.iterdir()): p.error('output must be empty')
    args.output.mkdir(parents=True, exist_ok=True)
    rows = []
    for source in sorted(args.collection.glob('*.milk')):
        row = candidate(source)
        if row: rows.append(row)
    rows.sort(key=lambda r: (r['static_cost'], r['name'].casefold()))
    chosen, authors = [], {}
    # Give smaller authors a place rather than filling every slot with one
    # author's many near-identical remixes. The remaining slots follow cost.
    for cap in (4, 128):
        for row in rows:
            author = row['name'].split(' - ')[0].casefold()
            if row in chosen or authors.get(author, 0) >= cap: continue
            chosen.append(row); authors[author] = authors.get(author, 0) + 1
            if len(chosen) == args.limit: break
        if len(chosen) == args.limit: break
    if not chosen: p.error('no eligible presets')
    for i, row in enumerate(chosen):
        row['file'] = 'active.milk' if i == 0 else row['name']
        shutil.copyfile(args.collection / row['name'], args.output / row['file'])
    (args.output/'manifest.json').write_text(json.dumps(chosen, ensure_ascii=False, indent=2)+'\n')
    readme = '''# PSP shaderless starter pack

Selected from your supplied collection for the 333 MHz performance target.
This is a static-cost shortlist, not a hardware FPS certification. Complex
transitions run TWO presets; use snapshot transitions or low visualization
resolution if desired. The renderer itself still interprets originals normally.

No HLSL, external textures, scripted loops or EEL memory operations. Expensive
pixel equations, large custom waves and shape instancing were excluded. Names
and author credits are preserved; active.milk is the first selection, not an
extra duplicate. manifest.json records original names, SHA-256 and selection
scores. NO preset formula or parameter was modified.

Original third-party presets keep their authors' rights. This locally assembled
pack does not grant redistribution rights or relicense those files under the
player's GPL. The Git repository carries the builder and manifest, not these
third-party presets. Assemble from your legally obtained collection; obtain
permission before publicly distributing the resulting pack.

Development/test presets remain in psp-client/presets in the source repository;
previous release presets are archived outside the installable app directory.
To install cleanly, move your old PSP presets folder aside before copying this
folder. Merely merging folders leaves the old tests visible. Missing previously
selected files fall back to active.milk; existing user presets are never deleted
from the Memory Stick by the application.

## Selection

'''
    readme += '\n'.join(f"- {r['file']} — original: {r['name']}; unchanged" for r in chosen)+'\n'
    (args.output/'README.md').write_text(readme)
    print(f'{len(chosen)} presets selected out of {len(rows)} eligible candidates')


if __name__ == '__main__': main()

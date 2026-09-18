#!/usr/bin/env python3
"""Inventory Desktop MilkDrop imports; stdout JSON, no inferred slider limits.

Usage: python3 tools/audit_milkdrop_import.py /path/to/vis_milk2/state.cpp
This is a source inventory, not a C++ parser or a compatibility certification.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path


def inventory(text):
    fields = []
    # Custom wave/shape imports construct buf immediately before GetFast*.
    custom = re.compile(
        r'sprintf\(buf,\s*"((?:wavecode|shapecode)_%d_%s)",\s*i,\s*"([^"]+)"\s*\)'
        r'\s*;\s*\w+\s*=\s*GetFast(Int|Float)\s*\(buf,([^,]+),\s*f2\)')
    direct = re.compile(r'GetFast(Int|Float)\s*\(\s*"([^"]+)"\s*,([^\n]+)')
    for match in custom.finditer(text):
        fields.append(dict(field=match[1].replace('%d', '{slot}').replace('%s', match[2]),
                           type=match[3], default_expression=match[4].strip(),
                           line=text.count('\n', 0, match.start()) + 1))
    for match in direct.finditer(text):
        # Preserve the source expression rather than guessing balanced C++ syntax.
        fields.append(dict(field=match[2], type=match[1],
                           source_tail=match[3].strip(),
                           line=text.count('\n', 0, match.start()) + 1))
    return sorted(fields, key=lambda row: row['line'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('state_cpp', type=Path)
    args = parser.parse_args()
    raw = args.state_cpp.read_bytes()
    fields = inventory(raw.decode('utf-8-sig'))
    if not fields:
        parser.error('No supported GetFast import declarations found')
    print(json.dumps(dict(source=str(args.state_cpp), sha256=hashlib.sha256(raw).hexdigest(),
                          note='Import declarations only; inspect drawing and formula code separately.',
                          fields=fields), indent=2))


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Embed our generated user code, refusing relocations/extra data or imports."""
import pathlib
import subprocess
import sys

obj, binary, output = map(pathlib.Path, sys.argv[1:])
relocs = subprocess.check_output(["psp-readelf", "-r", str(obj)], text=True)
symbols = subprocess.check_output(["psp-nm", "-u", str(obj)], text=True)
section = ""
bad_reloc = False
for line in relocs.splitlines():
    if line.startswith("Relocation section "):
        section = line.split("'")[1]
    if "R_MIPS" in line and section != ".rel.pdr":
        bad_reloc = True
# .pdr is discarded compiler procedure metadata, never copied to the PSP.
if bad_reloc or symbols.strip():
    raise SystemExit("POPS user payload must have no relocations or imports")
sections = subprocess.check_output(["psp-objdump", "-h", str(obj)], text=True)
for line in sections.splitlines():
    cols = line.split()
    if len(cols) >= 3 and cols[0].isdigit():
        name, size = cols[1], int(cols[2], 16)
        if size and (name.startswith((".rodata", ".sdata", ".sbss")) or name in (".data", ".bss")):
            raise SystemExit(f"Unexpected payload data: {name}")
data = binary.read_bytes()
if not data or len(data) > 8192 or len(data) % 4:
    raise SystemExit("Invalid POPS payload size")
lines = ["/* Generated from pops_payload.c; do not edit. */",
         "static const unsigned char pops_user_code[] __attribute__((aligned(4))) = {"]
lines += ["    " + ",".join(f"0x{x:02x}" for x in data[i:i+16]) + "," for i in range(0, len(data), 16)]
output.write_text("\n".join(lines) + "\n};\n")

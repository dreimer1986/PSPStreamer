"""Reject non-position-independent payloads and export user entry offsets."""
from pathlib import Path
import subprocess
import sys
obj,binary,output=map(Path,sys.argv[1:])
section=''
for line in subprocess.check_output(['psp-readelf','-r',str(obj)],text=True).splitlines():
    if line.startswith('Relocation section '):section=line.split("'")[1]
    if 'R_MIPS' in line and section!='.rel.pdr':raise SystemExit('Unexpected animation relocation')
if subprocess.check_output(['psp-nm','-u',str(obj)],text=True).strip():raise SystemExit('Unexpected animation import')
for line in subprocess.check_output(['psp-objdump','-h',str(obj)],text=True).splitlines():
    cols=line.split()
    if len(cols)>=3 and cols[0].isdigit():
        if int(cols[2],16) and (cols[1].startswith(('.rodata','.sdata','.sbss')) or cols[1] in ('.data','.bss')):
            raise SystemExit('Unexpected animation data')
data=binary.read_bytes()
if not data or len(data)>8192 or len(data)%4:raise SystemExit('Invalid animation code size')
symbols={p[2]:int(p[0],16) for line in subprocess.check_output(['psp-nm','--defined-only',str(obj)],text=True).splitlines()
         if len(p:=line.split())==3 and p[1]=='T'}
lines=['/* Generated; no Sony code or image data. */']
for name in ('load','bind','destroy'):
    off=symbols['xmb_animation_'+name]
    if off%4 or off>=len(data):raise SystemExit('Invalid entry')
    lines.append(f'#define XMB_ANIMATION_{name.upper()}_OFFSET {off}')
lines.append('static const unsigned char xmb_animation_code[] __attribute__((aligned(4))) = {')
lines+=['    '+','.join(f'0x{x:02x}' for x in data[i:i+16])+',' for i in range(0,len(data),16)]
output.write_text('\n'.join(lines)+'\n};\n')

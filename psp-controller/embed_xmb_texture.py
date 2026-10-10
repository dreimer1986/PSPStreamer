"""Build a self-contained user observer; reject runtime relocations/imports."""
from pathlib import Path
import subprocess
import sys

obj,binary,output=map(Path,sys.argv[1:])
section=''
for line in subprocess.check_output(['psp-readelf','-r',str(obj)],text=True).splitlines():
    if line.startswith('Relocation section '):section=line.split("'")[1]
    if 'R_MIPS' in line and section!='.rel.pdr':raise SystemExit('Unexpected payload relocation')
if subprocess.check_output(['psp-nm','-u',str(obj)],text=True).strip():
    raise SystemExit('Unexpected payload import')
symbols=subprocess.check_output(['psp-nm','--defined-only',str(obj)],text=True).splitlines()
if not any(line.split()==['00000000','T','xmb_texture_payload'] for line in symbols):
    raise SystemExit('Payload entry must be at text offset zero')
for line in subprocess.check_output(['psp-objdump','-h',str(obj)],text=True).splitlines():
    cols=line.split()
    if len(cols)>=3 and cols[0].isdigit():
        name,size=cols[1],int(cols[2],16)
        if size and (name.startswith(('.rodata','.sdata','.sbss')) or name in ('.data','.bss')):
            raise SystemExit('Unexpected payload data')
data=binary.read_bytes()
if not data or len(data)>8192 or len(data)%4:raise SystemExit('Invalid payload size')
lines=['/* Generated from xmb_texture_payload.c. */',
       'static const unsigned char xmb_texture_code[] __attribute__((aligned(4))) = {']
lines+=['    '+','.join(f'0x{x:02x}' for x in data[i:i+16])+',' for i in range(0,len(data),16)]
output.write_text('\n'.join(lines)+'\n};\n')

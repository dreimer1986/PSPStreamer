#!/usr/bin/env python3
"""Encode an existing video as immutable PSP swizzled RGB565 animation frames.

Uses no firmware data. --pic1 identifies the EBOOT background to replace.
"""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

MAGIC=0x31414258
HEADER=struct.Struct('<12I')
MAX_BYTES=8*1024*1024

def fnv(data, value=2166136261):
    for byte in data:value=((value^byte)*16777619)&0xffffffff
    return value

def geometry(w,h):
    if not 16<=w<=480 or not 8<=h<=272:raise ValueError('Unsupported dimensions')
    stride=((w+1)*2+15)&~15
    rows=(h+8)&~7
    return stride,rows,(stride*rows+63)&~63

def swizzle(raw,w,h):
    stride,rows,size=geometry(w,h)
    if len(raw)!=w*h*2:raise ValueError('Incomplete frame')
    padded=bytearray(stride*rows)
    for y in range(rows):
        row=raw[min(y,h-1)*w*2:(min(y,h-1)+1)*w*2]
        padded[y*stride:(y+1)*stride]=row+row[-2:]*((stride-w*2)//2)
    out=bytearray(size)
    for by in range(0,rows,8):
        for bx in range(0,stride,16):
            dst=by*stride+bx*8
            for iy in range(8):
                src=(by+iy)*stride+bx
                out[dst+iy*16:dst+(iy+1)*16]=padded[src:src+16]
    return out

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--video',required=True,type=Path)
    p.add_argument('--pic1',required=True,type=Path)
    p.add_argument('--output',required=True,type=Path)
    p.add_argument('--width',type=int,default=256)
    p.add_argument('--height',type=int,default=144)
    p.add_argument('--frames',type=int,default=96)
    p.add_argument('--fps',type=int,default=12)
    a=p.parse_args();stride,_,size=geometry(a.width,a.height)
    if not 2<=a.frames<=240 or not 1<=a.fps<=30 or size*a.frames>MAX_BYTES:
        p.error('Animation exceeds supported frame/rate/RAM budget')
    pic1=a.pic1.read_bytes()
    if not 0<len(pic1)<=1024000 or pic1[:8]!=b'\x89PNG\r\n\x1a\n':p.error('Expected PIC1 PNG')
    a.output.parent.mkdir(parents=True,exist_ok=True)
    cmd=['ffmpeg','-v','error','-i',str(a.video),'-an','-vf',
         f'fps={a.fps},scale={a.width}:{a.height}:flags=lanczos',
         '-frames:v',str(a.frames),'-f','rawvideo','-pix_fmt','bgr565le','pipe:1']
    proc=subprocess.Popen(cmd,stdout=subprocess.PIPE)
    temporary=None
    try:
        with tempfile.NamedTemporaryFile(dir=a.output.parent,prefix=a.output.name+'.',delete=False) as f:
            temporary=Path(f.name);f.write(bytes(HEADER.size));checksum=2166136261
            for i in range(a.frames):
                raw=proc.stdout.read(a.width*a.height*2)
                if len(raw)!=a.width*a.height*2:raise ValueError(f'Video ended at frame {i}')
                encoded=swizzle(raw,a.width,a.height)
                checksum=fnv(encoded,checksum);f.write(encoded)
            if proc.wait():raise ValueError('ffmpeg failed')
            f.seek(0);f.write(HEADER.pack(MAGIC,1,len(pic1),fnv(pic1),a.width,a.height,
                stride,size,a.frames,1000000//a.fps,checksum,0))
        temporary.replace(a.output);temporary=None
        print(f'{a.output}: {a.frames} frames, {a.width}x{a.height}, {a.fps} fps, {size*a.frames} bytes user RAM')
    finally:
        proc.stdout.close()
        if proc.poll() is None:proc.kill();proc.wait()
        if temporary is not None:temporary.unlink(missing_ok=True)

if __name__=='__main__':main()

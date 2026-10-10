#!/usr/bin/env python3
"""Encode an existing video as immutable PSP swizzled RGB565 animation frames.

Uses no firmware data. --app extracts PIC1 from an EBOOT without modifying it;
--pic1 accepts an already extracted background instead. Requires FFmpeg.
"""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

MAGIC=0x31414258
HEADER=struct.Struct('<12I')
MAX_BYTES=8*1024*1024

def read_pic1(path, is_pbp=False):
    with path.open('rb') as f:
        if is_pbp:
            header=f.read(40)
            if len(header)!=40 or header[:4]!=b'\x00PBP':raise ValueError('Not an EBOOT.PBP file')
            offsets=struct.unpack_from('<8I',header,8)
            f.seek(0,2);length=f.tell()
            if offsets[0]<40 or any(a>b for a,b in zip(offsets,offsets[1:])) or offsets[-1]>length:
                raise ValueError('Invalid PBP section offsets')
            size=offsets[5]-offsets[4] # PARAM, ICON0, ICON1, PIC0, PIC1, SND0, DATA.PSP, DATA.PSAR
            if not 0<size<=1024000:raise ValueError('App has no supported PIC1 background (maximum 1000 KiB)')
            f.seek(offsets[4]);data=f.read(size)
        else:data=f.read(1024001)
    if not 24<=len(data)<=1024000 or data[:8]!=b'\x89PNG\r\n\x1a\n' or data[12:16]!=b'IHDR':
        raise ValueError('Expected a PIC1 PNG')
    if struct.unpack_from('>II',data,16)!=(480,272):
        raise ValueError('Current runtime supports 480x272 PIC1 backgrounds only')
    return data

def asset_name(pic1):
    return f'{fnv(pic1):08X}-{len(pic1)}.xba'

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
    source=p.add_mutually_exclusive_group(required=True)
    source.add_argument('--app',type=Path,help='Target EBOOT.PBP; read-only extraction of PIC1')
    source.add_argument('--pic1',type=Path,help='Already extracted 480x272 PIC1 PNG')
    target=p.add_mutually_exclusive_group(required=True)
    target.add_argument('--output',type=Path,help='Explicit output file (legacy/custom use)')
    target.add_argument('--output-dir',type=Path,help='Destination animations folder; correct filename is automatic')
    p.add_argument('--width',type=int,default=256)
    p.add_argument('--height',type=int,default=144)
    p.add_argument('--frames',type=int,help='Exact frame count; overrides --duration')
    p.add_argument('--duration',type=float,default=8,help='Loop duration in seconds (default: 8)')
    p.add_argument('--fps',type=int,default=12)
    a=p.parse_args()
    try:
        stride,_,size=geometry(a.width,a.height)
        pic1=read_pic1(a.app or a.pic1,bool(a.app))
        if a.frames is None:a.frames=round(a.duration*a.fps)
    except (ValueError,OSError,OverflowError) as e:p.error(str(e))
    if not 2<=a.frames<=240 or not 1<=a.fps<=30 or size*a.frames>MAX_BYTES:
        p.error(f'Animation exceeds frame/rate/RAM budget: at this resolution at most {min(240,MAX_BYTES//size)} frames fit in 8 MiB')
    if a.output_dir:a.output=a.output_dir/asset_name(pic1)
    if a.output.suffix.lower()!='.xba':p.error('Output must use the .xba extension')
    if a.output.resolve() in (a.video.resolve(),(a.app or a.pic1).resolve()):
        p.error('Output must not overwrite the input video or target app/PIC1')
    a.output.parent.mkdir(parents=True,exist_ok=True)
    cmd=['ffmpeg','-v','error','-stream_loop','-1','-i',str(a.video),'-an','-vf',
         f'fps={a.fps},scale={a.width}:{a.height}:flags=lanczos',
         '-frames:v',str(a.frames),'-f','rawvideo','-pix_fmt','bgr565le','pipe:1']
    try:proc=subprocess.Popen(cmd,stdout=subprocess.PIPE)
    except FileNotFoundError:p.error('FFmpeg not found. Install ffmpeg and add it to PATH.')
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

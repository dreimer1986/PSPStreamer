"""Small Xbox transport: encoded access units and real MPEG-TS PTS (90 kHz).

XSM1 + uint32 flags (video=1, audio=2); records <c3xIq> then payload.
V = one MPEG-1 picture, A = one MPEG-1 Layer-II frame, E = clean end.
The client never uses pl_mpeg's frame-count clock. No B pictures are encoded.
"""
import re
import struct

MAX_PACKET = 256 * 1024


def record(kind, pts, data=b''):
    if len(data) > MAX_PACKET:
        raise ValueError('Xbox packet exceeds buffer limit')
    return struct.pack('<c3xIq', kind, len(data), pts) + data


def command(base, audio_only=False):
    cmd = list(base)
    for key in ('-profile:v', '-level:v', '-preset', '-tune', '-x264-params',
                '-flvflags', '-write_xing', '-id3v2_version', '-q:a'):
        if key in cmd:
            at = cmd.index(key)
            del cmd[at:at+2]
    if not audio_only:
        cmd[cmd.index('-c:v')+1] = 'mpeg1video'
        for key, value in (('-b:v','1500k'),('-maxrate','2000k'),('-bufsize','2000k')):
            cmd[cmd.index(key)+1] = value
        for key in ('-vf', '-filter_complex'):
            if key in cmd:
                at = cmd.index(key)+1
                cmd[at] = re.sub(r'fps=[0-9/]+', 'fps=24000/1001', cmd[at])
                cmd[at] = cmd[at].replace('720:480', '640:360')
        cmd[-3:-3] = ['-bf', '0', '-g', '12', '-shortest']
    cmd[cmd.index('-c:a')+1] = 'mp2'
    cmd[cmd.index('-ar')+1] = '48000'
    if '-af' in cmd:
        # Keep the master clock alive when a movie's audio track ends before
        # its picture. -shortest above bounds padding to video EOF, never infinity.
        cmd[cmd.index('-af')+1] = 'aresample=48000:first_pts=0' + (',apad' if not audio_only else '')
    if '-b:a' in cmd:
        cmd[cmd.index('-b:a')+1] = '192k'
    else:
        cmd[-3:-3] = ['-b:a','192k']
    cmd[-3:] = ['-mpegts_flags', '+resend_headers', '-pes_payload_size', '0',
                 '-muxdelay', '0', '-f', 'mpegts', 'pipe:1']
    return cmd


def timestamp(data):
    if len(data) < 5 or any(not data[i] & 1 for i in (0, 2, 4)):
        raise ValueError('Invalid Xbox PTS')
    return ((data[0] >> 1 & 7) << 30) | (data[1] << 22) | ((data[2] >> 1) << 15) | (data[3] << 7) | (data[4] >> 1)


def packets(pes):
    if len(pes) < 14 or not pes[7] & 0x80:
        raise ValueError('Xbox stream requires packet PTS')
    pts = timestamp(pes[9:14])
    payload = pes[9+pes[8]:]
    if pes[3] == 0xE0:
        if payload.count(b'\0\0\1\0') != 1:
            raise ValueError('Xbox requires one video picture per PES')
        yield record(b'V', pts, payload)
    elif pes[3] == 0xC0:
        at = 0
        while at < len(payload):
            h = payload[at:at+4]
            if len(h) != 4 or h[0] != 255 or h[1] & 0xFE != 0xFC:
                raise ValueError('Xbox requires MPEG-1 Layer-II audio')
            rate = (0,32,48,56,64,80,96,112,128,160,192,224,256,320,384,0)[h[2] >> 4]
            sample_rate = (44100,48000,32000,0)[h[2] >> 2 & 3]
            if not rate or sample_rate != 48000:
                raise ValueError('Xbox requires 48 kHz audio')
            size = 144000 * rate // sample_rate + (h[2] >> 1 & 1)
            if at + size > len(payload):
                raise ValueError('Truncated Xbox audio frame')
            yield record(b'A', pts, payload[at:at+size])
            pts += 2160  # 1152 actual samples at 48 kHz, in 90 kHz units.
            at += size


class Transport:
    def __init__(self):
        self.buffer = bytearray()
        self.pes = {}

    def feed(self, data):
        self.buffer.extend(data)
        while len(self.buffer) >= 188:
            ts = self.buffer[:188]
            del self.buffer[:188]
            if ts[0] != 0x47 or ts[1] & 0x80:
                raise ValueError('Invalid Xbox MPEG-TS packet')
            pid = (ts[1] & 31) << 8 | ts[2]
            if not ts[3] & 0x10:
                continue
            at = 5+ts[4] if ts[3] & 0x20 else 4
            if at > 188:
                raise ValueError('Invalid TS adaptation field')
            part = ts[at:]
            if ts[1] & 0x40:
                old = self.pes.pop(pid, None)
                if old:
                    yield from packets(old)
                if part[:3] != b'\0\0\1' or len(part) < 4 or part[3] not in (0xE0, 0xC0):
                    continue
                self.pes[pid] = bytearray()
            if pid not in self.pes:
                continue
            pes = self.pes[pid]
            pes.extend(part)
            if len(pes) > MAX_PACKET:
                raise ValueError('Xbox PES too large')
            if len(pes) >= 6:
                size = int.from_bytes(pes[4:6], 'big')
                if size and len(pes) >= size+6:
                    if len(pes) != size+6:
                        raise ValueError('Invalid PES length')
                    yield from packets(self.pes.pop(pid))

    def finish(self):
        if self.buffer:
            raise ValueError('Truncated MPEG-TS output')
        for pes in self.pes.values():
            yield from packets(pes)
        self.pes.clear()
        yield record(b'E', 0)

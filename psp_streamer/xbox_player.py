"""Small Xbox transport: encoded access units and real MPEG-TS PTS (90 kHz).

XSM1 + uint32 flags (video=1, audio=2); records <c3xIq> then payload.
V = one MPEG-1/2 picture, A = one MPEG-1 Layer-II frame, E = clean end.
The client never uses pl_mpeg's frame-count clock. No B pictures are encoded.
"""
import re
import struct

# Full-HD MPEG-2 I pictures can exceed 256 KiB despite the average bitrate.
# Allocation remains per actual packet; the client has a separate 1.5 MiB queue.
MAX_PACKET = 1024 * 1024
MAX_PES = MAX_PACKET + 264  # fixed + maximum optional PES header


def record(kind, pts, data=b''):
    if len(data) > MAX_PACKET:
        raise ValueError('Xbox packet exceeds buffer limit')
    return struct.pack('<c3xIq', kind, len(data), pts) + data


PROFILES = {'480p-low': (480,272,1500), '360p': (640,360,2000),
            '480p': (720,480,4000), '576p': (720,576,5000),
            '720p': (1280,720,9000), '1080p': (1920,1080,16000)}


def command(base, audio_only=False, size=None, codec='mpeg1', matrix='none', audio_quality='192k'):
    if audio_quality not in ('128k','192k','256k','320k','384k'):
        raise ValueError('Unsupported Xbox MP2 bitrate')
    if matrix not in ('none','dolby','dplii') or codec not in ('mpeg1', 'mpeg2') or size is not None and size not in PROFILES:
        raise ValueError('Unsupported Xbox video profile/codec')
    cmd = list(base)
    for key in ('-profile:v', '-level:v', '-preset', '-tune', '-x264-params',
                '-flvflags', '-write_xing', '-id3v2_version', '-q:a'):
        if key in cmd:
            at = cmd.index(key)
            del cmd[at:at+2]
    if not audio_only:
        cmd[cmd.index('-c:v')+1] = codec + 'video'
        width,height,bitrate = PROFILES[size] if size else (640,360,1500)
        for key, value in (('-b:v',f'{bitrate}k'),('-maxrate',f'{bitrate*2}k'),('-bufsize',f'{bitrate*2}k')):
            cmd[cmd.index(key)+1] = value
        for key in ('-vf', '-filter_complex'):
            if key in cmd:
                at = cmd.index(key)+1
                cmd[at] = re.sub(r'fps=[0-9/]+', 'fps=24000/1001', cmd[at])
                if size:
                    # Compose in square-pixel 16:9 space, including subtitles,
                    # then store SD anamorphically. Preserve source aspect ratio.
                    canvas_width = ((height*16//9+1)//2)*2
                    cmd[at] = re.sub(r'(?:720:480|480:272)', f'{canvas_width}:{height}', cmd[at])
                    cmd[at] = cmd[at].replace(
                        f'scale={canvas_width}:{height}:force_original_aspect_ratio=decrease',
                        f"scale=w='trunc(min({canvas_width},{height}*dar)/2)*2':h='trunc(min({height},{canvas_width}/dar)/2)*2',setsar=1")
                    tail = f',scale={width}:{height},setsar={16*height}/{9*width}'
                    if key == '-filter_complex':
                        cmd[at] = cmd[at].replace('[v]',tail+'[v]')
                    else:
                        cmd[at] += tail
                else:
                    cmd[at] = cmd[at].replace('720:480', '640:360')
        cmd[-3:-3] = ['-aspect', '16:9']
        cmd[-3:-3] = ['-bf', '0', '-g', '12', '-shortest']
    cmd[cmd.index('-c:a')+1] = 'mp2'
    cmd[cmd.index('-ar')+1] = '48000'
    # Keep the master clock alive when a movie's audio track ends before
    # its picture. -shortest bounds padding to video EOF, never infinity.
    audio_filter = ('aresample=48000:first_pts=0:out_chlayout=stereo:matrix_encoding='
                    + matrix + ':rematrix_maxval=1.0' + (',apad' if not audio_only else ''))
    if '-af' in cmd:
        cmd[cmd.index('-af')+1] = audio_filter
    else:
        cmd[-3:-3] = ['-af', audio_filter]
    if '-b:a' in cmd:
        cmd[cmd.index('-b:a')+1] = audio_quality
    else:
        cmd[-3:-3] = ['-b:a',audio_quality]
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
            if len(pes) > MAX_PES:
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

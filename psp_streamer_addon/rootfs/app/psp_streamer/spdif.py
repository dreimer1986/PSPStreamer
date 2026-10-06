"""Opt-in StreamMaster audio, independent of the established PSP MP3 path.

FFmpeg's Matroska output preserves shared video/audio timestamps and AVC
configuration. Convert it to the existing FLV video framing plus private audio
tags (F0 SMA1, little-endian rate/non_audio, stereo carrier words). These are
NOT general-purpose FLV files and must never be saved as offline PSP movies.
"""
import json
import struct

MODES = ('psp', 'spdif_pcm', 'spdif_auto_pcm', 'spdif_auto_ac3')
MAX_ELEMENT = 2 * 1024 * 1024


def command(base, source, track, mode, probe):
    if mode not in MODES[1:]:
        raise ValueError('Invalid optical audio mode')
    info = json.loads(probe(source, ['-select_streams', f'a:{track}',
                                  '-show_streams', '-of', 'json']).stdout)
    streams = info.get('streams', [])
    if not streams:
        raise ValueError('No audio track for optical output')
    audio = streams[0]
    codec = audio.get('codec_name')
    rate = int(audio.get('sample_rate') or 0)
    channels = int(audio.get('channels') or 2)
    copy = mode != 'spdif_pcm' and codec in ('ac3', 'dts') and rate in (32000, 44100, 48000)
    # DTS-HD is reduced to its existing core, never relabelled lossless audio.
    if codec == 'dts' and mode != 'spdif_pcm' and rate in (88200, 96000):
        copy = True
    cmd = list(base)
    for key in ('-c:a', '-ar', '-ac', '-af', '-b:a', '-q:a', '-flvflags',
                '-write_xing', '-id3v2_version'):
        while key in cmd:
            at = cmd.index(key)
            del cmd[at:at+2]
    cmd[-3:] = []  # output format and pipe
    if copy:
        cmd += ['-c:a', 'copy']
        if codec == 'dts':
            cmd += ['-bsf:a', 'dca_core']
        label = 'DTS core passthrough' if codec == 'dts' else 'AC-3 passthrough'
    elif mode == 'spdif_auto_ac3' and channels > 2:
        cmd += ['-c:a', 'ac3', '-ar', '48000', '-ac', str(min(channels, 6)), '-b:a', '640k']
        label = 'Dolby Digital (converted)'
    else:
        cmd += ['-c:a', 'pcm_s16le', '-ar', '48000', '-ac', '2']
        filters = []
        if '-af' in base:
            original_filter = base[base.index('-af')+1]
            for matrix in ('dolby', 'dplii'):
                if f'matrix_encoding={matrix}:' in original_filter:
                    filters.append(f'aresample=48000:out_chlayout=stereo:matrix_encoding={matrix}:rematrix_maxval=1.0')
                    break
        # Some codecs (notably TrueHD) decode very short frames. Without
        # batching these cause hundreds of synchronous USB writes per second.
        # Keep sample order/PTS, cap latency at 20 ms, never pad the final frame.
        filters += ['aresample=48000', 'asetnsamples=n=960:p=0']
        cmd += ['-af', ','.join(filters)]
        label = 'Stereo PCM (converted)'
    cmd += ['-avoid_negative_ts', 'make_zero', '-cluster_time_limit', '100',
            '-flush_packets', '1', '-f', 'matroska', 'pipe:1']
    return cmd, label


def vint(data, at=0, identifier=False):
    if at >= len(data):
        return None
    first = data[at]
    if not first:
        raise ValueError('Invalid Matroska variable integer')
    width = 9 - first.bit_length()
    if at + width > len(data):
        return None
    value = int.from_bytes(data[at:at+width], 'big')
    if not identifier:
        value &= (1 << (7*width)) - 1
    return value, width


def elements(data):
    at = 0
    while at < len(data):
        key = vint(data, at, True)
        length = vint(data, at + key[1]) if key else None
        if length is None:
            raise ValueError('Truncated Matroska header')
        start = at + key[1] + length[1]
        end = start + length[0]
        if end > len(data):
            raise ValueError('Truncated Matroska element')
        yield key[0], data[start:end]
        at = end


def tag(kind, pts_ms, data):
    if not 0 <= pts_ms < 2**32 or len(data) > MAX_ELEMENT:
        raise ValueError('Invalid optical stream packet')
    size = len(data)
    return (bytes([kind]) + size.to_bytes(3, 'big') + (pts_ms & 0xffffff).to_bytes(3, 'big')
            + bytes([pts_ms >> 24]) + b'\0\0\0' + data + (size+11).to_bytes(4, 'big'))


def burst(codec, data):
    """IEC61937 type I/II/III data bursts, including existing DTS core.

    Word order, data types and repetition periods follow FFmpeg spdifenc's
    IEC61937 muxer. No compressed sample arithmetic or volume scaling.
    """
    if codec == 'A_AC3':
        if len(data) < 7 or data[:2] != b'\x0b\x77' or data[5] >> 3 > 10:
            raise ValueError('Invalid AC-3 passthrough frame')
        index = data[4] >> 6
        rate = (48000, 44100, 32000, 0)[index]
        period, kind = 1536, 1 | ((data[5] & 7) << 8)
    elif codec == 'A_DTS':
        if len(data) < 12:
            raise ValueError('Truncated DTS core')
        if data[:4] in (b'\xfe\x7f\x01\x80', b'\xff\x1f\x00\xe8'):
            data = swap(data)
        if data[:4] == b'\x1f\xff\xe8\x00':
            # 14-bit storage words represent the same core, not an HD codec.
            bits = value = 0
            unpacked = bytearray()
            for at in range(0, len(data)-1, 2):
                value = (value << 14) | (int.from_bytes(data[at:at+2], 'big') & 0x3fff)
                bits += 14
                while bits >= 8:
                    bits -= 8
                    unpacked.append((value >> bits) & 255)
                value &= (1 << bits)-1
            data = bytes(unpacked)
        if data[:4] != b'\x7f\xfe\x80\x01':
            raise ValueError('Unsupported DTS stream (no normal core)')
        period = (((data[4] & 1) << 6 | data[5] >> 2) + 1) * 32
        size = ((data[5] & 3) << 12 | data[6] << 4 | data[7] >> 4) + 1
        if size > len(data):
            raise ValueError('Truncated DTS core frame')
        data = data[:size]
        rate = (0,8000,16000,32000,0,0,11025,22050,44100,0,0,12000,24000,48000,96000,192000)[(data[8] >> 2) & 15]
        kind = {512: 11, 1024: 12, 2048: 13}.get(period)
        if kind is None:
            raise ValueError('Unsupported DTS burst period')
    else:
        raise ValueError('Unsupported optical codec')
    if rate not in (32000, 44100, 48000) or len(data)+8 > period*4:
        raise ValueError('Compressed audio exceeds normal S/PDIF carrier')
    payload = struct.pack('<HHHH', 0xf872, 0x4e1f, kind, len(data)*8) + swap(data)
    return rate, payload + bytes(period*4-len(payload))


def swap(data):
    if len(data) & 1:
        data += b'\0'
    result = bytearray(len(data))
    result[::2], result[1::2] = data[1::2], data[::2]
    return bytes(result)


class Transport:
    def __init__(self, audio_only=False):
        self.buffer = bytearray()
        self.scale = 1000000
        self.cluster = 0
        self.tracks = {}
        self.started = False
        self.audio_only = audio_only

    def feed(self, data):
        self.buffer.extend(data)
        while self.buffer:
            key = vint(self.buffer, identifier=True)
            size = vint(self.buffer, key[1]) if key else None
            if size is None:
                break
            header = key[1]+size[1]
            if key[0] in (0x18538067, 0x1f43b675):
                # Streaming Segment/Cluster containers can have unknown sizes.
                del self.buffer[:header]
                continue
            if size[0] > MAX_ELEMENT:
                raise ValueError('Matroska element exceeds optical buffer limit')
            end = header + size[0]
            if len(self.buffer) < end:
                break
            payload = bytes(self.buffer[header:end])
            del self.buffer[:end]
            if key[0] == 0x1549a966:
                for field, value in elements(payload):
                    if field == 0x2ad7b1:
                        self.scale = int.from_bytes(value, 'big')
            elif key[0] == 0x1654ae6b:
                if self.started:
                    raise ValueError('Changing optical stream tracks')
                yield b'FLV\1' + bytes([4 if self.audio_only else 5]) + b'\0\0\0\x09\0\0\0\0'
                for field, value in elements(payload):
                    if field != 0xae:
                        continue
                    entry = dict(elements(value))
                    number = int.from_bytes(entry[0xd7], 'big')
                    codec = entry.get(0x86, b'').decode('ascii')
                    self.tracks[number] = codec
                    if codec == 'V_MPEG4/ISO/AVC':
                        yield tag(9, 0, b'\x17\0\0\0\0' + entry[0x63a2])
                    elif codec not in ('A_PCM/INT/LIT', 'A_AC3', 'A_DTS'):
                        raise ValueError('Unexpected optical stream codec')
                self.started = True
            elif key[0] == 0xe7:
                self.cluster = int.from_bytes(payload, 'big')
            elif key[0] == 0xa3:
                yield from self.block(payload)
            elif key[0] == 0xa0:
                for field, value in elements(payload):
                    if field == 0xa1:
                        yield from self.block(value)

    def block(self, data):
        track = vint(data)
        if not track or len(data) < track[1]+3 or not self.started:
            raise ValueError('Invalid optical media block')
        offset = track[1]
        relative = int.from_bytes(data[offset:offset+2], 'big', signed=True)
        pts = (self.cluster+relative)*self.scale//1000000
        flags = data[offset+2]
        if flags & 6:
            # Our FFmpeg producer writes one packet per block (no lacing).
            raise ValueError('Unexpected laced optical media block')
        payload = data[offset+3:]
        codec = self.tracks.get(track[0])
        if codec == 'V_MPEG4/ISO/AVC':
            yield tag(9, pts, bytes([0x17 if flags & 128 else 0x27, 1, 0, 0, 0])+payload)
            return
        if codec == 'A_PCM/INT/LIT':
            rate, carrier, compressed = 48000, payload, 0
        else:
            rate, carrier = burst(codec, payload)
            compressed = 1
        if len(carrier) % 4:
            raise ValueError('Incomplete stereo carrier frame')
        for at in range(0, len(carrier), 3840):
            header = b'\xf0SMA1' + struct.pack('<II', rate, compressed)
            yield tag(8, pts+(at//4)*1000//rate, header+carrier[at:at+3840])

    def finish(self):
        if self.buffer or not self.started:
            raise ValueError('Truncated optical media stream')
        yield tag(9, 0, b'\x17\x02\0\0\0')

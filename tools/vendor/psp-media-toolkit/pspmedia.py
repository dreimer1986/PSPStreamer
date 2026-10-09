#!/usr/bin/env python3
"""
mp42pmf — PSP homebrew icon media converter.

Produces XMB-compatible ICON1.PMF (PSMF0014 video) and SND0.AT3 (ATRAC3+ RIFF)
matching Sony's own icon format byte-for-byte in structure. Reverse-engineered
from official Sony samples (game icon PMF + SND0.AT3).
Requirements:
  - ffmpeg / ffprobe on PATH (video + audio preprocessing, verification)
  - for SND0: an ATRAC3+ encoder — either atracdenc.exe (open source) or
    Sony at3tool.exe, found on PATH or next to this script / the input file.

Usage:
  python mp42pmf.py analyze  <file.pmf | file.at3>
  python mp42pmf.py convert  <in.mp4>  [-o ICON1.PMF] [--budget-kb 450]
  python mp42pmf.py snd0     <in.wav>  [-o SND0.AT3]  [--encoder auto]
  python mp42pmf.py all      <video> <audio> [--outdir DIR]
Key format facts (from Sony reference):
  video: H.264 Main@2.1 CABAC, 144x80, 29.97 fps, ref=1, no B-frames,
         HRD 600 kbps / 600 kbit CPB, full range, AUD + SEI per frame
  mux:   2048-byte packs @ mux_rate 25000 (10 Mbps), one PES per pack,
         chunks = IDR GOPs, each led by a private-stream-2 (0xBF) AU-size
         index packet, closed by 0xBE padding; PTS/DTS on chunk starts and
         every >=16th frame; P-STD buffer 80 KB declared in three places.
  audio: 44.1 kHz stereo RIFF with fact + smpl loop chunks. Retail SND0s are
         ATRAC3+ 48-128 kbps (at3tool); plain ATRAC3 LP4 66 kbps (atracdenc)
         is hardware-verified working. CRITICAL: fact must never claim more
         samples than frames can decode (samples + delay + 368 <= frames*spf)
         or the XMB plays nothing.
  XMB budget: keep each file under ~500 KB.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
from collections import Counter
# ---------------------------------------------------------------- constants

TICKS_PER_FRAME = 3003           # 90 kHz ticks @ 29.97 fps
FIRST_PTS = 90000
PACK_SIZE = 2048
PACK_HDR_SIZE = 14
MUX_RATE = 25000                 # units of 50 bytes/s -> 10 Mbps
VIDEO_W, VIDEO_H = 144, 80
BUDGET_KB_DEFAULT = 450          # keep well under the ~500 KB XMB limit
HRD_KBPS = 600                   # Sony icon rate regime
# Verbatim Sony bytes
SYS_HEADER = bytes.fromhex('000001bb000c80c35180f07fb9e050bde008')
PES_EXT_PSTD = bytes.fromhex('1e6050')            # ext flags + P-STD 80 KB
PACK_TAIL = bytes.fromhex('0186a3f8')             # mux_rate 25000 + stuffing 0
STREAM_ENTRY = bytes([0xE0, 0x00, 0x20, 0x50, 0, 0, 0, 0, 0, 0, 0, 0,
                      VIDEO_W // 16, VIDEO_H // 16, 0, 0])
# Sony SND0.AT3 templates
AT3_FMT_CHUNK = bytes.fromhex(
    'feff020044ac0000a01f00007801000022000008030000'
    '00bfaa23e958cb7144a119fffa01e4ce620100282e0000'
    '000000000000')                                  # 52 bytes, 376 B frames
AT3_SMPL_TMPL = bytearray.fromhex(
    '0000000000000000945800003c000000'
    '000000000000000000000000'
    '0100000018000000'
    '0000000000000000'
    '040b00008f6e0700'
    '0000000000000000')                              # 60 bytes; loop patched
AT3_FRAME_BYTES = 376
AT3_SAMPLES_PER_FRAME = 2048
AT3_DELAY = 2820
SCR_DELAY_TICKS = 76850          # SCR0 = DTS0 - this (Sony: CPB prefill time)


def be16(v): return struct.pack('>H', v)
def be32(v): return struct.pack('>I', v)
def rd16(b, o): return struct.unpack_from('>H', b, o)[0]
def rd32(b, o): return struct.unpack_from('>I', b, o)[0]


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        sys.exit('command failed: %s\n%s' % (' '.join(map(str, cmd)), r.stderr[-3000:]))
    return r
# ---------------------------------------------------------------- H.264 ES

class BitReader:
    def __init__(self, d):
        self.d, self.p = d, 0

    def bit(self):
        v = (self.d[self.p >> 3] >> (7 - (self.p & 7))) & 1
        self.p += 1
        return v

    def bits(self, n):
        v = 0
        for _ in range(n):
            v = (v << 1) | self.bit()
        return v
    def ue(self):
        z = 0
        while self.bit() == 0:
            z += 1
            if z > 30:
                raise ValueError('bad exp-golomb')
        return (1 << z) - 1 + self.bits(z) if z else 0

    def se(self):
        k = self.ue()
        return (k + 1) // 2 if k % 2 else -(k // 2)

def strip_ep3(nal):
    out, i = bytearray(), 0
    while i < len(nal):
        if i + 2 < len(nal) and nal[i] == 0 and nal[i + 1] == 0 and nal[i + 2] == 3:
            out += nal[i:i + 2]
            i += 3
        else:
            out.append(nal[i])
            i += 1
    return bytes(out)

def split_nals(es):
    """Annex-B split; returns list of (start_code_len, nal_bytes)."""
    out, i, n = [], 0, len(es)
    starts = []
    while True:
        j = es.find(b'\x00\x00\x01', i)
        if j < 0:
            break
        sc = 4 if j > 0 and es[j - 1] == 0 else 3
        starts.append((j - (sc - 3), j + 3))
        i = j + 3
    for k, (st, body) in enumerate(starts):
        end = starts[k + 1][0] if k + 1 < len(starts) else n
        out.append((body - st, es[body:end]))
    return out

def split_aus(es, drop_sei_types=(5,), drop_nal_types=(12,)):
    """Split raw ES into access units at AUDs, cleaning unwanted NALs.
    Returns list of (au_bytes, is_idr)."""
    aus, cur, cur_idr = [], bytearray(), False

    def flush():
        nonlocal cur, cur_idr
        if cur:
            aus.append((bytes(cur), cur_idr))
        cur, cur_idr = bytearray(), False
    for sclen, nal in split_nals(es):
        t = nal[0] & 0x1F
        if t == 9:
            flush()
        if t in drop_nal_types:
            continue
        if t == 6 and len(nal) > 1 and nal[1] in drop_sei_types:
            continue
        cur += (b'\x00\x00\x00\x01' if sclen == 4 else b'\x00\x00\x01') + nal
        if t == 5:
            cur_idr = True
    flush()
    if not aus or not aus[0][1]:
        sys.exit('encoded stream does not start with an IDR access unit')
    return aus

def parse_sps(nal):
    r = BitReader(strip_ep3(nal[1:]))
    s = {'profile': r.bits(8), 'constraints': r.bits(8), 'level': r.bits(8),
         'sps_id': r.ue()}
    if s['profile'] in (100, 110, 122, 244, 44, 83, 86, 118, 128):
        if r.ue() == 3:
            r.bit()
        r.ue(); r.ue(); r.bit()
        if r.bit():
            raise ValueError('scaling lists unsupported by this parser')
    s['log2_max_frame_num'] = r.ue() + 4
    s['poc_type'] = r.ue()
    if s['poc_type'] == 0:
        r.ue()
    elif s['poc_type'] == 1:
        r.bit(); r.se(); r.se()
        for _ in range(r.ue()):
            r.se()
    s['ref_frames'] = r.ue()
    r.bit()
    w = (r.ue() + 1) * 16
    hm = r.ue() + 1
    mbs_only = r.bit()
    h = hm * 16 * (2 - mbs_only)
    if not mbs_only:
        r.bit()
    r.bit()
    if r.bit():
        cl, cr, ct, cb = r.ue(), r.ue(), r.ue(), r.ue()
        w, h = w - 2 * (cl + cr), h - 2 * (ct + cb)
    s['size'] = (w, h)
    if r.bit():                                   # VUI
        if r.bit():
            if r.bits(8) == 255:
                r.bits(32)
        if r.bit():
            r.bit()
        if r.bit():
            r.bits(3)
            s['full_range'] = r.bit()
            if r.bit():
                r.bits(24)
        if r.bit():
            r.ue(); r.ue()
        if r.bit():
            nu, ts = r.bits(32), r.bits(32)
            r.bit()
            s['fps'] = ts / 2.0 / nu
        nal_h = r.bit()
        if nal_h:
            s['hrd_bps'], s['hrd_cpb'] = _hrd(r)
        vcl_h = r.bit()
        if vcl_h:
            _hrd(r)
        if nal_h or vcl_h:
            r.bit()
    return s

def _hrd(r):
    cnt = r.ue() + 1
    brs, cps = r.bits(4), r.bits(4)
    bps = cpb = 0
    for _ in range(cnt):
        bps = (r.ue() + 1) << (6 + brs)
        cpb = (r.ue() + 1) << (4 + cps)
        r.bit()
    r.bits(5); r.bits(5); r.bits(5); r.bits(5)
    return bps, cpb


def parse_pps(nal):
    r = BitReader(strip_ep3(nal[1:]))
    return {'pps_id': r.ue(), 'sps_id': r.ue(), 'cabac': r.bit()}
# ---------------------------------------------------------------- PSMF mux

def scr_bytes(v27):
    base, ext = divmod(v27, 300)
    b = [0] * 6
    b[0] = 0x44 | ((base >> 27) & 0x38) | ((base >> 28) & 0x03)
    b[1] = (base >> 20) & 0xFF
    b[2] = ((base >> 12) & 0xF8) | 0x04 | ((base >> 13) & 0x03)
    b[3] = (base >> 5) & 0xFF
    b[4] = ((base << 3) & 0xF8) | 0x04 | ((ext >> 7) & 0x03)
    b[5] = ((ext << 1) & 0xFE) | 0x01
    return bytes(b)

def pts_field(prefix, v):
    return bytes([prefix | ((v >> 29) & 0x0E) | 1,
                  (v >> 22) & 0xFF, ((v >> 14) & 0xFE) | 1,
                  (v >> 7) & 0xFF, ((v << 1) & 0xFE) | 1])

def build_bf_payload(au_sizes, first_chunk):
    hdr = (0, 0, 1, 1, 0, 0) if first_chunk else (0, 1, 1, 2, 0, 0)
    n = len(au_sizes)
    out = bytearray(b'\x01\xe0')
    for v in hdr:
        out += be16(v)
    out += be16(2 + 4 * n)                       # table bytes
    out += be16(n)
    for i, sz in enumerate(au_sizes):
        out += be16(0x0000 if i == n - 1 else 0x0080) + be16(sz)
    return bytes(out)

def mux_psmf(aus, out_path):
    """aus: list of (au_bytes, is_idr). Writes a Sony-layout PSMF file."""
    # --- chunks = IDR GOPs
    chunks = []
    for i, (au, idr) in enumerate(aus):
        if idr or not chunks:
            chunks.append([])
        chunks[-1].append(i)

    au_pts = [FIRST_PTS + TICKS_PER_FRAME * i for i in range(len(aus))]
    stream = bytearray()
    last_stamped = 0
    for ci, chunk in enumerate(chunks):
        es = b''.join(aus[i][0] for i in chunk)
        # AU start offsets within this chunk's ES
        offs, acc = [], 0
        for i in chunk:
            offs.append(acc)
            acc += len(aus[i][0])
        bf = build_bf_payload([len(aus[i][0]) for i in chunk], ci == 0)
        pos = 0
        first_pes = True
        while pos < len(es):
            pack = bytearray(b'\x00\x00\x01\xba' + b'\x00' * 6 + PACK_TAIL)
            space = PACK_SIZE - PACK_HDR_SIZE
            if ci == 0 and first_pes:
                pack += SYS_HEADER
                space -= len(SYS_HEADER)
            if first_pes:
                pack += b'\x00\x00\x01\xbf' + be16(len(bf)) + bf
                space -= 6 + len(bf)
            # decide PES header size; stamping depends on which AU starts
            # inside the payload window, which itself depends on header size
            def first_au_in(win):
                return next((k for k, o in enumerate(offs)
                             if pos <= o < pos + win), None)
            hlen = 0
            for _ in range(3):
                payload = min(len(es) - pos, space - 9 - hlen)
                kf = first_au_in(payload)
                stamp = first_pes or (kf is not None
                                      and chunk[kf] >= last_stamped + 16)
                want = (13 if first_pes else 10) if stamp else 0
                if want == hlen:
                    break
                hlen = want
            payload = min(len(es) - pos, space - 9 - hlen)
            kf = first_au_in(payload)
            hdr = bytearray()
            flags = 0x00
            if hlen and kf is not None:
                ai = chunk[kf]
                pts = au_pts[ai]
                hdr += pts_field(0x30, pts) + pts_field(0x10, pts - TICKS_PER_FRAME)
                flags = 0xC0
                if hlen == 13:
                    hdr += PES_EXT_PSTD
                    flags = 0xC1
                last_stamped = ai
            # if the qualifying AU fell out of the shrunk window, hlen bytes
            # simply become PES header stuffing (flags stay 0)
            leftover = space - 9 - hlen - payload
            stuff = leftover if 0 < leftover < 6 else 0    # too small for BE
            hlen_field = hlen + stuff
            pes_len = 3 + hlen_field + payload
            pack += (b'\x00\x00\x01\xe0' + be16(pes_len)
                     + bytes([0x81, flags, hlen_field])
                     + hdr + b'\xff' * (hlen_field - len(hdr))
                     + es[pos:pos + payload])
            pos += payload
            leftover -= stuff
            if leftover:
                pack += b'\x00\x00\x01\xbe' + be16(leftover - 6) + b'\xff' * (leftover - 6)
            assert len(pack) == PACK_SIZE
            stream += pack
            first_pes = False
    # --- SCR patch pass
    npacks = len(stream) // PACK_SIZE
    scr0 = (au_pts[0] - TICKS_PER_FRAME - SCR_DELAY_TICKS) * 300
    total = len(aus) * TICKS_PER_FRAME
    pace = max(44237, min(TICKS_PER_FRAME * 300, total * 300 // max(1, npacks)))
    for i in range(npacks):
        stream[i * PACK_SIZE + 4:i * PACK_SIZE + 10] = scr_bytes(scr0 + i * pace)
    # --- PSMF header
    hdr = bytearray(0x800)
    hdr[0:8] = b'PSMF0014'
    hdr[8:12] = be32(0x800)
    hdr[12:16] = be32(len(stream))
    last_ts = FIRST_PTS + TICKS_PER_FRAME * len(aus)
    hdr[0x50:0x54] = be32(0x3E)
    hdr[0x54:0x5A] = FIRST_PTS.to_bytes(6, 'big')
    hdr[0x5A:0x60] = last_ts.to_bytes(6, 'big')
    hdr[0x60:0x64] = be32(MUX_RATE)
    hdr[0x64:0x68] = be32(90000)
    hdr[0x68:0x6A] = b'\x01\x01'
    hdr[0x6A:0x6E] = be32(0x24)
    hdr[0x6E:0x74] = FIRST_PTS.to_bytes(6, 'big')
    hdr[0x74:0x7A] = last_ts.to_bytes(6, 'big')
    hdr[0x7A:0x7C] = be16(1)
    hdr[0x7C:0x80] = be32(0x12)
    hdr[0x80:0x82] = be16(1)
    hdr[0x82:0x92] = STREAM_ENTRY
    with open(out_path, 'wb') as f:
        f.write(hdr)
        f.write(stream)
    return len(hdr) + len(stream), len(chunks)
# ---------------------------------------------------------------- analyze
def demux_psmf(data):
    """Returns (packs, pes_list, video_es, bf_payloads)."""
    pos, cur = 0x800, -1
    packs, pes, ves, bfs = [], [], bytearray(), []
    while pos < len(data) - 4:
        code = rd32(data, pos)
        if code == 0x1BA:
            cur += 1
            packs.append(pos)
            pos += PACK_HDR_SIZE + (data[pos + 13] & 7)
        elif code == 0x1BB:
            pos += 6 + rd16(data, pos + 4)
        elif code == 0x1B9:
            pos += 4
        elif (code >> 8) == 1:
            sid, ln = code & 0xFF, rd16(data, pos + 4)
            rec = dict(pack=cur, sid=sid, len=ln, pts=None, dts=None)
            if sid == 0xBF:
                bfs.append(data[pos + 6:pos + 6 + ln])
            elif sid in (0xBD, 0xBE, 0xE0):
                flags, hl = data[pos + 7], data[pos + 8]
                if sid != 0xBE:
                    p = pos + 9
                    if flags & 0x80:
                        b = data[p:p + 5]
                        rec['pts'] = ((b[0] >> 1) & 7) << 30 | b[1] << 22 | \
                                     (b[2] >> 1) << 15 | b[3] << 7 | b[4] >> 1
                    if flags & 0x40:
                        b = data[p + 5:p + 10]
                        rec['dts'] = ((b[0] >> 1) & 7) << 30 | b[1] << 22 | \
                                     (b[2] >> 1) << 15 | b[3] << 7 | b[4] >> 1
                if sid == 0xE0:
                    ves += data[pos + 9 + hl:pos + 6 + ln]
            pes.append(rec)
            pos += 6 + ln
        else:
            pos += 1
    return packs, pes, bytes(ves), bfs

def analyze_pmf(path):
    data = open(path, 'rb').read()
    print('== %s  (%d bytes / %.1f KB) ==' % (os.path.basename(path),
                                              len(data), len(data) / 1024))
    if data[:4] != b'PSMF':
        sys.exit('not a PSMF file')
    ver = data[4:8].decode()
    first = int.from_bytes(data[0x54:0x5A], 'big')
    last = int.from_bytes(data[0x5A:0x60], 'big')
    ns = rd16(data, 0x80)
    e = data[0x82:0x92]
    print('PSMF%s streamSize=%#x firstTS=%d lastTS=%d (%.2fs) streams=%d' %
          (ver, rd32(data, 12), first, last, (last - first) / 90000, ns))
    print('stream entry: %s  (P-STD %02X%02X, %dx%d)' %
          (e.hex(), e[2], e[3], e[12] * 16, e[13] * 16))
    packs, pes, es, bfs = demux_psmf(data)
    cnt = Counter('%02X' % r['sid'] for r in pes)
    print('packs=%d PES=%s' % (len(packs), dict(cnt)))
    aus = split_aus(es, drop_sei_types=(), drop_nal_types=())
    sizes = [len(a) for a, _ in aus]
    print('AUs=%d (IDR=%d) ES=%d bytes, AU size min/max=%d/%d' %
          (len(aus), sum(1 for _, i in aus if i), len(es), min(sizes), max(sizes)))
    exp_last = first + TICKS_PER_FRAME * len(aus)
    print('lastTS grid check: header=%d expected=%d %s' %
          (last, exp_last, 'OK' if last == exp_last else 'MISMATCH'))
    for _, nal in split_nals(es):
        t = nal[0] & 0x1F
        if t == 7:
            s = parse_sps(nal)
            print('SPS: profile=%d level=%d ref=%d size=%s full_range=%s '
                  'fps=%.3f HRD=%s/%s bits' %
                  (s['profile'], s['level'], s['ref_frames'], s['size'],
                   s.get('full_range'), s.get('fps', 0),
                   s.get('hrd_bps'), s.get('hrd_cpb')))
        elif t == 8:
            print('PPS: cabac=%d' % parse_pps(nal)['cabac'])
            break
    # BF consistency
    k = 0
    ok = True
    for bi, bf in enumerate(bfs):
        n = rd16(bf, 16)
        tbl = [rd16(bf, 18 + 4 * j + 2) for j in range(n)]
        real = sizes[k:k + n]
        if tbl != real:
            ok = False
            print('BF%d MISMATCH (n=%d)' % (bi, n))
        k += n
    print('BF index packets: %d chunks, %s (cover %d/%d AUs)' %
          (len(bfs), 'sizes OK' if ok and k == len(aus) else 'BROKEN',
           k, len(aus)))
    # PTS stamp gaps
    stamped = [r['pts'] for r in pes if r['sid'] == 0xE0 and r['pts'] is not None]
    if len(stamped) > 1:
        gaps = [b - a for a, b in zip(stamped, stamped[1:])]
        print('PTS stamps=%d maxGap=%.3fs (must be <0.7s)' %
              (len(stamped), max(gaps) / 90000))
    print('size budget: %.1f KB %s' % (len(data) / 1024,
          'OK' if len(data) <= 512 * 1024 else 'OVER ~500KB XMB LIMIT'))
    return dict(aus=len(aus), es=es, size=len(data))

def analyze_at3(path):
    data = open(path, 'rb').read()
    print('== %s  (%d bytes / %.1f KB) ==' % (os.path.basename(path),
                                              len(data), len(data) / 1024))
    if data[:4] != b'RIFF':
        sys.exit('not RIFF')
    pos, ba = 12, AT3_FRAME_BYTES
    while pos < len(data) - 8:
        cid, sz = data[pos:pos + 4], struct.unpack_from('<I', data, pos + 4)[0]
        if cid == b'fmt ':
            tag, ch, sr, abps, ba, _ = struct.unpack_from('<HHIIHH', data, pos + 8)
            print('fmt: tag=%#x ch=%d rate=%d %d bps blockAlign=%d %s' %
                  (tag, ch, sr, abps * 8, ba,
                   'AT3+GUID OK' if data[pos + 8 + 24:pos + 8 + 40] ==
                   AT3_FMT_CHUNK[24:40] else ''))
        elif cid == b'fact':
            print('fact:', struct.unpack_from('<2I', data, pos + 8))
        elif cid == b'smpl':
            st, en = struct.unpack_from('<2I', data, pos + 8 + 44)
            print('smpl: loop %d..%d' % (st, en))
        elif cid == b'data':
            print('data: %d bytes = %.1f frames' % (sz, sz / ba))
        pos += 8 + sz + (sz & 1)
# ---------------------------------------------------------------- convert

def ffprobe_duration(path):
    r = run(['ffprobe', '-v', 'error', '-show_entries', 'format=duration',
             '-of', 'default=nw=1:nk=1', path])
    return float(r.stdout.strip().splitlines()[0])

def encode_video(src, budget_kb, tmpdir):
    dur = ffprobe_duration(src)
    if dur > 30:
        print('warning: %.1fs source is long for an icon; consider trimming' % dur)
    kbps = min(HRD_KBPS, int(budget_kb * 8 / dur * 0.94))
    vf = ('fps=30000/1001,'
          'scale=%d:%d:force_original_aspect_ratio=decrease:out_range=full,'
          'pad=%d:%d:(ow-iw)/2:(oh-ih)/2,format=yuv420p'
          % (VIDEO_W, VIDEO_H, VIDEO_W, VIDEO_H))
    x264 = ('keyint=48:min-keyint=8:scenecut=40:ref=1:bframes=0:weightp=0:'
            'vbv-maxrate=%d:vbv-bufsize=%d:nal-hrd=vbr:aud=1:range=pc'
            % (HRD_KBPS, HRD_KBPS))
    es_path = os.path.join(tmpdir, 'video.264')
    log = os.path.join(tmpdir, 'x264pass')
    for p in (1, 2):
        run(['ffmpeg', '-y', '-v', 'error', '-i', src, '-an',
             '-vf', vf, '-c:v', 'libx264', '-preset', 'veryslow',
             '-profile:v', 'main', '-level:v', '2.1',
             '-b:v', '%dk' % kbps, '-pass', str(p), '-passlogfile', log,
             '-x264-params', x264, '-f', 'h264', es_path])
    print('encoded %.2fs @ %dk -> %d bytes ES' %
          (dur, kbps, os.path.getsize(es_path)))
    return es_path

def cmd_convert(src, out, budget_kb):
    with tempfile.TemporaryDirectory() as tmp:
        for attempt in range(3):
            es = open(encode_video(src, budget_kb, tmp), 'rb').read()
            aus = split_aus(es)
            size, nchunks = mux_psmf(aus, out)
            print('muxed %s: %d AUs, %d chunks, %.1f KB' %
                  (out, len(aus), nchunks, size / 1024))
            if size <= (budget_kb + 30) * 1024:
                break
            budget_kb = int(budget_kb * 0.85)
            print('over budget, retrying at %d KB' % budget_kb)
    verify_pmf(out, aus)

def verify_pmf(path, src_aus):
    print('--- verify %s ---' % path)
    info = analyze_pmf(path)
    got = split_aus(info['es'], drop_sei_types=(), drop_nal_types=())
    same = len(got) == len(src_aus) and all(
        a == b[0] for (a, _), b in zip(got, src_aus))
    print('demux round-trip: %d AUs, byte-identical to input: %s'
          % (len(got), same))
    # decode check
    with tempfile.TemporaryDirectory() as tmp:
        esf = os.path.join(tmp, 'v.264')
        open(esf, 'wb').write(info['es'])
        r = run(['ffprobe', '-v', 'error', '-count_frames', '-select_streams',
                 'v', '-show_entries', 'stream=nb_read_frames',
                 '-of', 'default=nw=1:nk=1', esf])
        frames = int(r.stdout.strip() or 0)
        print('ffmpeg decode: %d frames (expect %d) %s' %
              (frames, len(src_aus),
               'OK' if frames == len(src_aus) else 'MISMATCH'))
# ---------------------------------------------------------------- SND0.AT3
def find_at3_encoder(explicit, search_dirs):
    names = {'at3tool': ['at3tool.exe', 'psp_at3tool.exe'],
             'atracdenc': ['atracdenc.exe', 'atracdenc']}
    order = [explicit] if explicit in names else ['at3tool', 'atracdenc']
    for kind in order:
        for n in names[kind]:
            for d in search_dirs:
                p = os.path.join(d, n)
                if os.path.isfile(p):
                    return kind, p
            from shutil import which
            p = which(n)
            if p:
                return kind, p
    return None, None

def cmd_snd0(src, out, encoder, codec='at3'):
    """codec: 'at3'  = plain ATRAC3 LP4 66 kbps — hardware-verified working
              'at3p' = ATRAC3+ (retail style; needs at3tool for 48-128 kbps;
                       atracdenc's 352.8 kbps is rejected by the PSP)"""
    here = os.path.dirname(os.path.abspath(__file__))
    kind, enc = find_at3_encoder(encoder,
                                 [here, os.path.dirname(os.path.abspath(src))])
    if not enc:
        sys.exit('no ATRAC3 encoder found.\n'
                 '  option A (open source): download atracdenc win build\n'
                 '    https://github.com/dcherednik/atracdenc/releases '
                 '(atracdenc-win-x86_0.2.1.zip)\n'
                 '    and put atracdenc.exe next to this script.\n'
                 '  option B: put Sony at3tool.exe next to this script.')
    with tempfile.TemporaryDirectory() as tmp:
        wav = os.path.join(tmp, 'in44k.wav')
        run(['ffmpeg', '-y', '-v', 'error', '-i', src,
             '-ar', '44100', '-ac', '2', '-sample_fmt', 's16', wav])
        nsamples = wav_sample_count(wav)
        if nsamples > 30 * 44100:
            print('warning: >30s audio; XMB SND0 limit is ~30 seconds')
        if kind == 'at3tool':
            # Sony encoder: real-game SND0s are its direct output (AT3+ at
            # 48..128 kbps, -wholeloop for the loop chunk) — pass through.
            raw = os.path.join(tmp, 'out.at3')
            run([enc, '-e', '-br', '64', '-wholeloop', wav, raw])
            blob = open(raw, 'rb').read()
            if blob[:4] != b'RIFF':
                sys.exit('unexpected at3tool output (not RIFF)')
            extract_at3p_frames(blob)          # validates the AT3+ GUID
            with open(out, 'wb') as f:
                f.write(blob)
        elif codec == 'at3p':
            raw = os.path.join(tmp, 'out.oma')
            run([enc, '-e', 'atrac3plus', '-i', wav, '-o', raw])
            frames = extract_at3p_frames(open(raw, 'rb').read())
            print('warning: atracdenc AT3+ is fixed at 352.8 kbps, which the '
                  'PSP REJECTS (hardware-tested). Use the default at3 codec, '
                  'or at3tool.exe for retail-style AT3+ 48-128 kbps')
            write_snd0_at3p(out, frames, nsamples)
        else:
            oma = os.path.join(tmp, 'out.oma')
            for name in ('atrac3_lp4', 'atrac3_lp', 'atrac3'):
                r = subprocess.run([enc, '-e', name, '-i', wav, '-o', oma],
                                   capture_output=True, text=True)
                if r.returncode == 0:
                    print('atracdenc mode: %s' % name)
                    break
            else:
                sys.exit('atracdenc ATRAC3 encode failed:\n' + r.stderr[-2000:])
            remux = os.path.join(tmp, 'remux.wav')
            run(['ffmpeg', '-y', '-v', 'error', '-i', oma,
                 '-c:a', 'copy', '-f', 'wav', remux])
            write_snd0_at3(out, remux, nsamples)
    print('wrote %s (%.1f KB)' % (out, os.path.getsize(out) / 1024))
    analyze_at3(out)
    # decode sanity via ffmpeg
    run(['ffmpeg', '-v', 'error', '-y', '-i', out, '-f', 'null', '-'])
    print('ffmpeg decode: OK')

def wav_sample_count(path):
    b = open(path, 'rb').read()
    pos, ba, data_sz = 12, 4, 0
    while pos < len(b) - 8:
        cid, sz = b[pos:pos + 4], struct.unpack_from('<I', b, pos + 4)[0]
        if cid == b'fmt ':
            ba = struct.unpack_from('<H', b, pos + 8 + 12)[0]
        elif cid == b'data':
            data_sz = sz
        pos += 8 + sz + (sz & 1)
    return data_sz // ba

def extract_at3p_frames(blob):
    """Accept RIFF .at3 or OMA/EA3 output; return raw frame data + frame size."""
    if blob[:4] == b'RIFF':
        pos, ba, data = 12, AT3_FRAME_BYTES, None
        while pos < len(blob) - 8:
            cid, sz = blob[pos:pos + 4], struct.unpack_from('<I', blob, pos + 4)[0]
            if cid == b'fmt ':
                if blob[pos + 8 + 24:pos + 8 + 40] != AT3_FMT_CHUNK[24:40]:
                    sys.exit('encoder output is not ATRAC3+ (wrong fmt GUID) — '
                             'check encoder mode/bitrate flags')
                ba = struct.unpack_from('<H', blob, pos + 8 + 12)[0]
            elif cid == b'data':
                data = blob[pos + 8:pos + 8 + sz]
            pos += 8 + sz + (sz & 1)
        return data, ba
    if blob[:3] == b'ea3' or b'EA3' in blob[:0x1000]:
        # OMA: optional ea3 tag header, then 96-byte EA3 header, then frames
        off = 0
        if blob[:3] == b'ea3':
            off = 10 + struct.unpack('>I', bytes(
                [0, blob[6] & 0x7F, blob[8] & 0x7F, blob[9] & 0x7F]))[0]
            # syncsafe-ish; fall back to scan
            if blob[off:off + 3] != b'EA3':
                off = blob.find(b'EA3\x01')
        else:
            off = blob.find(b'EA3\x01')
        params = blob[off + 33:off + 36]
        fsz = ((struct.unpack('>I', b'\x00' + params)[0] & 0x3FF) + 1) * 8
        return blob[off + 96:], fsz
    sys.exit('unrecognized encoder output container')

AT3_END_PAD = 368        # at3tool leaves >=368 samples of tail margin

def clamp_fact_samples(nsamples, n_frames, spf, delay):
    """XMB rejects files whose fact chunk claims more samples than the
    frames can decode. Known-good at3tool files keep samples + delay +
    368 <= frames*spf; mirror that."""
    cap = n_frames * spf - delay - AT3_END_PAD
    if cap <= 0:
        sys.exit('audio too short after delay/padding accounting')
    if nsamples > cap:
        print('fact clamp: %d -> %d samples (frames=%d)' % (nsamples, cap, n_frames))
    return min(nsamples, cap)

def assemble_at3(out, fmt, data, nsamples, delay):
    smpl = bytearray(AT3_SMPL_TMPL)
    struct.pack_into('<I', smpl, 44, delay)
    struct.pack_into('<I', smpl, 48, delay + nsamples - 1)   # last sample index
    fact = struct.pack('<2I', nsamples, delay)
    chunks = (b'fmt ' + struct.pack('<I', len(fmt)) + fmt
              + b'fact' + struct.pack('<I', len(fact)) + fact
              + b'smpl' + struct.pack('<I', len(smpl)) + smpl
              + b'data' + struct.pack('<I', len(data)) + data)
    with open(out, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 4 + len(chunks)) + b'WAVE' + chunks)

def write_snd0_at3p(out, frames_info, nsamples):
    data, frame_bytes = frames_info
    if not data:
        sys.exit('encoder produced no frames')
    fmt = bytearray(AT3_FMT_CHUNK)
    if frame_bytes != AT3_FRAME_BYTES:
        bytes_per_sec = frame_bytes * 44100 // AT3_SAMPLES_PER_FRAME
        struct.pack_into('<I', fmt, 8, bytes_per_sec)
        struct.pack_into('<H', fmt, 12, frame_bytes)
        fmt[43] = frame_bytes // 8 - 1
        print('note: %d-byte frames (%d bps) - differs from Sony 376/64768'
              % (frame_bytes, bytes_per_sec * 8))
    nsamples = clamp_fact_samples(nsamples, len(data) // frame_bytes,
                                  AT3_SAMPLES_PER_FRAME, AT3_DELAY)
    assemble_at3(out, fmt, data, nsamples, AT3_DELAY)

def write_snd0_at3(out, remuxed_wav, nsamples):
    """Plain ATRAC3: take ffmpeg-remuxed RIFF (correct 0x0270 fmt incl.
    codec extradata), keep fmt + data, add Sony-style fact/smpl chunks."""
    b = open(remuxed_wav, 'rb').read()
    pos, fmt, data = 12, None, None
    while pos < len(b) - 8:
        cid, sz = b[pos:pos + 4], struct.unpack_from('<I', b, pos + 4)[0]
        if cid == b'fmt ':
            fmt = b[pos + 8:pos + 8 + sz]
        elif cid == b'data':
            data = b[pos + 8:pos + 8 + sz]
        pos += 8 + sz + (sz & 1)
    if not fmt or not data:
        sys.exit('remuxed ATRAC3 RIFF is missing fmt/data')
    tag = struct.unpack_from('<H', fmt, 0)[0]
    if tag != 0x0270:
        sys.exit('remux produced fmt tag %#x, expected ATRAC3 0x0270' % tag)
    ba, = struct.unpack_from('<H', fmt, 12)
    print('ATRAC3: blockAlign=%d (%d bps)' % (ba, ba * 8 * 44100 // 1024))
    nsamples = clamp_fact_samples(nsamples, len(data) // ba, 1024, 1024)
    assemble_at3(out, fmt, data, nsamples, 1024)
# ---------------------------------------------------------------- PBP

PBP_NAMES = ['PARAM.SFO', 'ICON0.PNG', 'ICON1.PMF', 'PIC0.PNG',
             'PIC1.PNG', 'SND0.AT3', 'DATA.PSP', 'DATA.PSAR']


def pbp_read(path):
    d = open(path, 'rb').read()
    if d[:4] != b'\x00PBP':
        sys.exit('%s is not a PBP' % path)
    offs = list(struct.unpack_from('<8I', d, 8)) + [len(d)]
    return [d[offs[i]:offs[i + 1]] for i in range(8)], d[4:8]

def pbp_write(path, entries, version):
    off, offs = 40, []
    for e in entries:
        offs.append(off)
        off += len(e)
    with open(path, 'wb') as f:
        f.write(b'\x00PBP' + version + struct.pack('<8I', *offs))
        for e in entries:
            f.write(e)

def sfo_set_title(sfo, title):
    sfo = bytearray(sfo)
    _, _, key_off, data_off, count = struct.unpack_from('<4sIIII', sfo, 0)
    for i in range(count):
        ko, _, _, mx, do = struct.unpack_from('<HHIII', sfo, 20 + 16 * i)
        key = sfo[key_off + ko:sfo.index(b'\0', key_off + ko)].decode()
        if key == 'TITLE':
            enc = title.encode('utf-8')[:mx - 1]
            sfo[data_off + do:data_off + do + mx] = enc + b'\0' * (mx - len(enc))
            struct.pack_into('<I', sfo, 20 + 16 * i + 4, len(enc) + 1)
            return bytes(sfo)
    sys.exit('TITLE not found in PARAM.SFO')

def cmd_pbp(donor, out, icon1=None, snd0=None, title=None):
    entries, ver = pbp_read(donor)
    if icon1:
        entries[2] = open(icon1, 'rb').read()
    if snd0:
        entries[5] = open(snd0, 'rb').read()
    if title:
        entries[0] = sfo_set_title(entries[0], title)
    pbp_write(out, entries, ver)
    print('wrote %s (%.1f KB): %s' % (out, os.path.getsize(out) / 1024,
          {n: len(e) for n, e in zip(PBP_NAMES, entries) if e}))
# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    sub = ap.add_subparsers(dest='cmd', required=True)
    a = sub.add_parser('analyze')
    a.add_argument('file')
    c = sub.add_parser('convert')
    c.add_argument('src')
    c.add_argument('-o', '--out', default='ICON1.PMF')
    c.add_argument('--budget-kb', type=int, default=BUDGET_KB_DEFAULT)
    s = sub.add_parser('snd0')
    s.add_argument('src')
    s.add_argument('-o', '--out', default='SND0.AT3')
    s.add_argument('--encoder', default='auto')
    s.add_argument('--codec', default='at3', choices=['at3', 'at3p'],
                   help='at3 = plain ATRAC3 66kbps via atracdenc — hardware-'
                        'verified working (default). at3p = ATRAC3+ (retail '
                        'style, 48-128k, needs at3tool; atracdenc can only '
                        'do 352.8k which the PSP rejects)')
    al = sub.add_parser('all')
    al.add_argument('video')
    al.add_argument('audio')
    al.add_argument('--outdir', default='.')
    al.add_argument('--budget-kb', type=int, default=BUDGET_KB_DEFAULT)
    p = sub.add_parser('pbp', help='repack a donor EBOOT.PBP with new '
                                   'ICON1.PMF / SND0.AT3 / title')
    p.add_argument('donor')
    p.add_argument('-o', '--out', default='EBOOT.PBP')
    p.add_argument('--icon1')
    p.add_argument('--snd0')
    p.add_argument('--title')
    args = ap.parse_args()
    if args.cmd == 'analyze':
        if open(args.file, 'rb').read(4) == b'RIFF':
            analyze_at3(args.file)
        else:
            analyze_pmf(args.file)
    elif args.cmd == 'convert':
        cmd_convert(args.src, args.out, args.budget_kb)
    elif args.cmd == 'snd0':
        cmd_snd0(args.src, args.out, args.encoder, args.codec)
    elif args.cmd == 'all':
        pmf = os.path.join(args.outdir, 'ICON1.PMF')
        at3 = os.path.join(args.outdir, 'SND0.AT3')
        cmd_convert(args.video, pmf, args.budget_kb)
        cmd_snd0(args.audio, at3, 'auto')
        total = os.path.getsize(pmf) + os.path.getsize(at3)
        print('combined ICON1.PMF + SND0.AT3 = %.1f KB %s (XMB limit ~500 KB '
              'combined)' % (total / 1024,
                             'OK' if total <= 500 * 1024 else 'OVER'))
    elif args.cmd == 'pbp':
        cmd_pbp(args.donor, args.out, args.icon1, args.snd0, args.title)

if __name__ == '__main__':
    main()

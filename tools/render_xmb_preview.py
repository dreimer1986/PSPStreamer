#!/usr/bin/env python3
"""Render an original 8-second XMB concept; MP4/WAV are NOT PMF/ATRAC assets.

Uses the existing ICON0 without changing it. Requires numpy, Pillow and ffmpeg.
No Sony encoder or third-party sound samples are used.
"""
import argparse
import math
from pathlib import Path
import subprocess
import wave

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
FRAMES, FPS, RATE = 240, 30000 / 1001, 44100
DURATION = FRAMES / FPS


def soundtrack(path):
    t = np.arange(round(DURATION * RATE)) / RATE
    # Original restrained A-minor pad; smooth attack/release at loop boundary.
    fade = np.minimum(np.minimum(t / .25, (DURATION - t) / .35), 1).clip(0, 1)
    pad = sum(np.sin(2 * np.pi * f * t) for f in (110, 220, 261.6256, 329.6276)) / 4
    pad *= .13 * (.8 + .2 * np.sin(2 * np.pi * t / DURATION))
    arp = np.zeros_like(t)
    for i, start in enumerate(np.arange(2.25, DURATION - .4, .25)):
        local = t - start
        env = np.exp(-np.maximum(local, 0) * 17) * (local >= 0)
        freq = (440, 523.251, 659.255, 880)[i % 4]
        arp += .065 * env * np.sin(2 * np.pi * freq * local)
    rng = np.random.default_rng(20261009)
    noise = rng.standard_normal(len(t))
    noise = np.convolve(noise, np.ones(19) / 19, mode='same')
    charge = np.sin(np.pi * np.clip(t / 2, 0, 1)) ** 2
    riser = charge * (.10 * noise + .065 * np.sin(2 * np.pi * (70 * t + 70 * t * t)))
    local = np.maximum(t - 2, 0)
    sparkle = (t >= 2) * np.exp(-local * 8) * .09 * (
        np.sin(2 * np.pi * 1318.51 * local) + .4 * np.sin(2 * np.pi * 2093 * local))
    mono = pad + arp + riser + sparkle
    side = .018 * np.sin(2 * np.pi * 330.4 * t) * fade
    stereo = np.column_stack((mono * fade + side, mono * fade - side))
    peak = np.max(np.abs(stereo))
    if peak > .55:
        stereo *= .55 / peak
    with wave.open(str(path), 'wb') as out:
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes((stereo * 32767).astype('<i2').tobytes())


def render_frame(base, index):
    # Work at 4x icon resolution; all coordinates below use the 144x80 layout.
    scale = 4
    t = index / FPS
    phase = index / FRAMES
    layer = Image.new('RGBA', base.size)
    pen = ImageDraw.Draw(layer)
    def line(points, color, width=1):
        pen.line([(round(x * scale), round(y * scale)) for x, y in points],
                 fill=color, width=max(1, round(width * scale)))
    def dot(x, y, r, color):
        pen.ellipse(((x-r)*scale, (y-r)*scale, (x+r)*scale, (y+r)*scale), fill=color)
    # Repeatable particles follow the film's perspective into the screen.
    for i in range(13):
        p = (phase * 4 + i / 13) % 1
        x = 8 + 94 * p
        y = 38 + math.sin(i * 2.4) * 16 * (1-p) ** 2
        alpha = int(190 * math.sin(math.pi * p) ** .7)
        line([(x-4*(1-p), y), (x, y)], (40, 190, 255, alpha), .6)
        dot(x, y, .4 + .55*(1-p), (150, 90, 255, alpha))
    pulse = .5 + .5 * math.sin(2 * math.pi * phase * 4)
    # Soft screen border, not a full-screen flash or obscuring replacement.
    line([(84,29),(110,29),(110,47),(84,47),(84,29)],
         (50, 210, 255, int(15 + 25*pulse)), .45)
    flare = math.exp(-((t-2)/.12)**2)
    line([(96,35),(101,38),(96,41),(96,35)],
         (140, 240, 255, int(35 + 45*pulse + 60*flare)), .5)
    dot(98, 38, 1.5 + flare, (110, 235, 255, int(10 + flare*25)))
    # Highlight traverses the PSP top edge and fades at both ends.
    sweep = (phase * 2) % 1
    dot(57 + sweep*65, 19, .8, (180, 225, 255, int(180*math.sin(math.pi*sweep)**2)))
    glow = layer.filter(ImageFilter.GaussianBlur(1.5 * scale))
    frame = Image.alpha_composite(Image.alpha_composite(base, glow), layer)
    return frame.convert('RGB')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    base = Image.open(ROOT / 'psp-client/assets/icon0.png').convert('RGBA')
    assert base.size == (144, 80), base.size
    base = base.resize((576, 320), Image.Resampling.LANCZOS)
    wav = args.output / 'PSPStreamer-CyberStream.wav'
    mp4 = args.output / 'PSPStreamer-CyberStream.mp4'
    soundtrack(wav)
    cmd = ['ffmpeg','-hide_banner','-loglevel','error','-y',
           '-f','rawvideo','-pixel_format','rgb24','-video_size','576x320',
           '-framerate','30000/1001','-i','pipe:0','-i',str(wav),
           '-c:v','libx264','-preset','slow','-crf','18','-pix_fmt','yuv420p',
           '-c:a','aac','-b:a','128k','-movflags','+faststart','-shortest',str(mp4)]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    try:
        for i in range(FRAMES):
            proc.stdin.write(render_frame(base, i).tobytes())
    finally:
        proc.stdin.close()
    if proc.wait():
        raise RuntimeError('ffmpeg preview encoding failed')
    print(mp4)
    print(wav)


if __name__ == '__main__':
    main()

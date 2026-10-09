#!/usr/bin/env python3
"""Cyberpunk v3 preview: detailed flyby and original cinematic stereo sound."""
import argparse
import math
import wave
from pathlib import Path

import numpy as np
from PIL import Image
from render_xmb_cyberpunk import Scene, ART, encode, smooth
from render_xmb_preview import FPS, FRAMES, RATE, DURATION


def soundtrack(path):
    t = np.arange(round(DURATION * RATE)) / RATE
    rng = np.random.default_rng(3109)
    noise = rng.standard_normal(len(t))
    low_noise = np.convolve(noise, np.ones(31) / 31, mode='same')
    stereo = np.zeros((len(t), 2))

    def mix(signal, pan=0):
        p = np.broadcast_to(np.asarray(pan), t.shape).clip(-1, 1)
        stereo[:, 0] += signal * np.sqrt((1-p)/2)
        stereo[:, 1] += signal * np.sqrt((1+p)/2)

    # Mysterious minor/add9 bed, followed by an accelerating activation sequence.
    env = smooth(t/.6) * (1-smooth((t-6.2)/1.8))
    for i, f in enumerate((55, 110, 130.8128, 164.8138, 246.9417)):
        mix(.075*env*np.sin(2*np.pi*f*t + .35*np.sin(t*(i+1))), (i-2)/3)
    charge = np.sin(np.pi*np.clip(t/1.8, 0, 1))**2
    mix(charge*(.23*low_noise+.09*np.sin(2*np.pi*(65*t+115*t*t))))
    for start, weight in ((1.75, 1), (2.25, .45), (6.05, .8)):
        u = np.maximum(t-start, 0)
        e = (t >= start)*np.exp(-u*3.4)
        phase = 2*np.pi*(48*u+45*(1-np.exp(-u*13))/13)
        # Harmonics carry impact on the PSP's small speakers as well as a sub.
        mix(weight*e*(.65*np.sin(phase)+.23*np.sin(2*phase)+.10*np.sin(3*phase)))
        mix(weight*(t >= start)*np.exp(-u*24)*low_noise*.8)
    for i, start in enumerate(np.arange(2.0, 4.1, .21)):
        u = np.maximum(t-start, 0)
        f = (440, 523.251, 659.255, 987.767)[i % 4]
        tone = (t >= start)*np.exp(-u*11)*np.sin(2*np.pi*f*u)*.14
        mix(tone, math.sin(i*1.7)*.7)
    # Same trajectory/timing as the picture: starts in the screen at right,
    # rushes past the listener leftwards, then falls in pitch and recedes.
    p = np.clip((t-3.45)/2.5, 0, 1)
    fly_env = np.sin(np.pi*p)**1.5
    freq = 180+290*(1-smooth((t-4.3)/1.2))
    phase = 2*np.pi*np.cumsum(freq)/RATE
    fly = fly_env*(.35*low_noise + .10*noise + .19*np.sin(phase)
                   + .10*np.sin(phase*2.01) + .12*np.sin(phase*.25))
    mix(fly, .8-1.8*smooth(p))
    # Dispersed stereo echo, then a short high-frequency shimmer.
    dry = stereo.copy()
    for delay, gain in ((.113, .18), (.229, .12), (.383, .08), (.617, .05)):
        n = round(delay*RATE)
        stereo[n:] += dry[:-n, ::-1]*gain
    u = np.maximum(t-6.05, 0)
    mix((t >= 6.05)*np.exp(-u*2.8)*.1*(np.sin(2*np.pi*987.767*u)
                                                   + .4*np.sin(2*np.pi*1480*u)))
    stereo *= (smooth(t/.025)*(1-smooth((t-(DURATION-.5))/.5)))[:, None]
    stereo = np.tanh(stereo*1.4)
    stereo *= .88/max(np.max(abs(stereo)), 1e-9)
    with wave.open(str(path), 'wb') as out:
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes((stereo*32767).astype('<i2').tobytes())


class FlybyScene(Scene):
    def __init__(self):
        super().__init__()
        self.ship = Image.open(ART/'flyby-ship.png').convert('RGBA')

    def frame(self, index, wallpaper=False):
        im = super().frame(index, wallpaper).convert('RGBA')
        t = index/FPS
        if 3.45 < t < 5.95:
            p = (t-3.45)/2.5
            # Accelerating approach, subtle bank; supersampled source with
            # Lanczos reduction, not the low-poly PSP runtime geometry.
            x = 692-1340*p**1.65
            y = 299+260*p**1.6-85*math.sin(math.pi*p)
            width = round(30+620*p**1.35)
            sprite = self.ship.resize((width, round(width*self.ship.height/self.ship.width)),
                                      Image.Resampling.LANCZOS)
            sprite = sprite.rotate(-8*math.sin(math.pi*p), Image.Resampling.BICUBIC, expand=True)
            alpha = float(smooth(p/.12))
            sprite.putalpha(sprite.getchannel('A').point(lambda a: round(a*alpha)))
            im.alpha_composite(sprite, (round(x-sprite.width/2), round(y-sprite.height/2)))
        return im.convert('RGB')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    wav = args.output/'Cyberpunk-Flyby-Sound.wav'
    soundtrack(wav)
    scene = FlybyScene()
    encode(scene, args.output/'Cyberpunk-Icon-Flyby.mp4', wav)
    scene.frame(round(4.6*FPS)).save(args.output/'Flyby-Vorschau.png')
    print(args.output)


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Opt-in integration test using synthetic A/V and a text subtitle, no library media.

Run from /app in a built container: python3 - < tools/check_hardware_encoding.py
or locally: PYTHONPATH=. python3 tools/check_hardware_encoding.py
"""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

from psp_streamer.acceleration import Acceleration, hardware_command
from psp_streamer.server import ffmpeg_command


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--backend', choices=['vaapi', 'nvenc'], default='vaapi')
    parser.add_argument('--device', default='/dev/dri/renderD128')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='psp-hardware-test-') as directory:
        base = Path(directory)
        subtitle = base/'test.srt'
        subtitle.write_text('1\n00:00:00,000 --> 00:00:00,800\nPSP hardware test ÄÖÜ\n')
        source = base/'source.mkv'
        subprocess.run(['ffmpeg','-v','error','-f','lavfi','-i','testsrc2=size=1280x720:rate=24',
                        '-f','lavfi','-i','sine=frequency=440:sample_rate=44100','-i',str(subtitle),
                        '-t','0.8','-c:v','libx264','-preset','ultrafast','-c:a','libmp3lame','-c:s','srt',str(source)],
                       check=True,timeout=30)
        for tv in (False,True):
            for fps in ('20','24000/1001'):
                ok, detail = Acceleration.probe(args.backend,args.device,tv,fps)
                assert ok, detail
                for track in (-1,0):
                    output = base/f'output-{tv}-{fps.replace("/","_")}-{track}.flv'
                    command = ffmpeg_command(source,0,'flv',subtitle_track=track,tv_output=tv,video_fps=fps)
                    command = hardware_command(command,args.backend,args.device)
                    command[-1] = str(output)
                    subprocess.run(command,check=True,timeout=30)
                    data=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams',
                                                            '-show_packets','-of','json',str(output)],timeout=10))
                    video=next(s for s in data['streams'] if s['codec_type']=='video')
                    audio=next(s for s in data['streams'] if s['codec_type']=='audio')
                    assert video['profile']=='Constrained Baseline' and video['level']==30
                    assert video['has_b_frames']==0 and video['pix_fmt']=='yuv420p'
                    assert (video['width'],video['height'])==((720,480) if tv else (480,272))
                    assert audio['codec_name']=='mp3' and audio['sample_rate']=='44100'
                    vp=[p for p in data['packets'] if p['codec_type']=='video']
                    ap=[p for p in data['packets'] if p['codec_type']=='audio']
                    assert len(vp)>8 and len(ap)>8
                    assert all(p['pts']==p['dts'] for p in vp)
                    assert all(a['pts']<b['pts'] for a,b in zip(vp,vp[1:]))
                    assert abs(float(vp[0]['pts_time'])-float(ap[0]['pts_time']))<.1
                    subprocess.run(['ffmpeg','-v','error','-xerror','-i',str(output),'-f','null','-'],
                                   check=True,timeout=15)
                    print(f'PASS {args.backend} TV={tv} FPS={fps} subtitles={track>=0}',flush=True)


if __name__=='__main__':
    main()

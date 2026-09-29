#!/usr/bin/env python3
"""Isolated Main/CABAC comparison; never changes server settings or its queue."""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys
import threading

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from psp_streamer.server import Library, MediaItem, ffmpeg_command, parse_srt_cues
from psp_streamer.offline import OfflineQueue


def command_for(source, audio, *, hardware, device, tv, seconds, start):
    command = ffmpeg_command(source, audio, 'flv', tv_output=tv, start_seconds=start)
    if hardware:
        for option in ('-preset', '-tune', '-x264-params'):
            i = command.index(option)
            del command[i:i+2]
        command[command.index('-c:v')+1] = 'h264_vaapi'
        command[command.index('-level:v')+1] = '30'
        i = command.index('-vf')+1
        # Keep fps, scale and padding on CPU; only upload the finished NV12 frame.
        command[i] = command[i].removesuffix('format=yuv420p') + 'format=nv12,hwupload'
        command[1:1] = ['-vaapi_device', device]
        command[-1:-1] = ['-coder', 'cabac', '-bf', '0', '-refs', '1', '-g', '64',
                          '-aud', '1', '-rc_mode', 'VBR', '-async_depth', '1',
                          '-bsf:v', 'filter_units=remove_types=6']
    command[-1:-1] = ['-t', str(seconds)]
    return command


def validate(movie, tv, hardware=False):
    result = subprocess.run(['ffprobe', '-v', 'error', '-show_streams', '-show_packets',
                             '-of', 'json', str(movie)], capture_output=True, check=True)
    data = json.loads(result.stdout)
    video = next(s for s in data['streams'] if s['codec_type'] == 'video')
    expected = (720, 480) if tv else (480, 272)
    if (video['codec_name'] != 'h264' or video.get('profile') != 'Main' or
            video.get('level') != 30 or video.get('has_b_frames') != 0 or
            video.get('pix_fmt') != 'yuv420p' or (video['width'], video['height']) != expected):
        raise ValueError('Unexpected H.264 profile/geometry/reordering')
    packets = [p for p in data['packets'] if p['stream_index'] == video['index']]
    if not packets or any('pts' not in p or p.get('pts') != p.get('dts') for p in packets):
        raise ValueError('Video PTS/DTS are missing or reordered')
    if any(a['pts'] >= b['pts'] for a, b in zip(packets, packets[1:])):
        raise ValueError('Video timestamps are not increasing')
    if not any(s['codec_name'] == 'mp3' for s in data['streams']):
        raise ValueError('Choose a source/audio track with sound')
    trace = subprocess.run(['ffmpeg', '-hide_banner', '-i', str(movie), '-map', '0:v:0',
                            '-frames:v', '1', '-c:v', 'copy', '-bsf:v', 'trace_headers',
                            '-f', 'null', '-'], capture_output=True, text=True, check=True).stderr
    if not re.search(r'entropy_coding_mode_flag\s+.*= 1\b', trace):
        raise ValueError('CABAC not confirmed in PPS (trace_headers required)')
    if hardware and not re.search(r'max_num_ref_frames\s+.*= 1\b', trace):
        raise ValueError('Single reference frame not confirmed in SPS')
    subprocess.run(['ffmpeg', '-v', 'error', '-xerror', '-i', str(movie), '-f', 'null', '-'], check=True)
    return data, trace


def create_case(source, output, hardware, device, tv, seconds, start, audio):
    library = Library([source.parent])
    builder = lambda s, a, *args, **kwargs: command_for(
        s, a, hardware=hardware, device=device, tv=tv, seconds=seconds, start=start)
    queue = OfflineQueue(output, library, builder, parse_srt_cues, threading.Semaphore(1))
    job = queue.prepare({'id': library.encode(MediaItem(0, source.name)),
                         'audio': audio, 'subtitle': -1, 'profile': 'tv' if tv else 'normal'})
    label = ('VAAPI' if hardware else 'Software') + ('-TV' if tv else '-LCD')
    job.update(name=label+'.flv', state='encoding')
    output.mkdir(parents=True, exist_ok=True)
    queue._save(job)
    folder = output / job['job']
    original_run = queue._run
    with (folder/'encoder.log').open('w') as errors:
        def run(command, current_job, **kwargs):
            if '-c:v' in command:
                (folder/'command.txt').write_text(shlex.join(command)+'\n')
            kwargs['stderr'] = errors
            return original_run(command, current_job, **kwargs)
        queue._run = run
        try:
            queue._convert(job)
            data, trace = validate(folder/job['name'], tv, hardware)
            (folder/'probe.json').write_text(json.dumps(data, indent=2))
            (folder/'headers.txt').write_text(trace)
            job['duration'] = min(seconds, max(0, job['duration']-start))
            queue._save(job)
            # This folder is copied directly to the PSP, not exported through
            # the server ZIP route which normally compacts the disk manifest.
            (folder/'job.json').write_text(json.dumps(job, ensure_ascii=False,
                                                   separators=(',', ':')), encoding='utf-8')
            (folder/'ready').write_text('1')
        except BaseException:
            job.update(state='error', error='Test failed; see encoder.log')
            queue._save(job)
            raise
        finally:
            queue.close()
    print(f'{label}: checked, copy this whole folder to PSP/VIDEO/PSPStreamer/: {folder}', flush=True)
    return folder


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('--output', type=Path, required=True, help='Separate test directory, NOT the server queue')
    p.add_argument('--device', default='/dev/dri/renderD128')
    p.add_argument('--profile', choices=['lcd', 'tv'], default='tv')
    p.add_argument('--encoder', choices=['both', 'software', 'vaapi'], default='both')
    p.add_argument('--seconds', type=int, default=60, choices=range(1, 301), metavar='1..300')
    p.add_argument('--start', type=float, default=0)
    p.add_argument('--audio', type=int, default=0)
    args = p.parse_args()
    if not args.source.is_file() or args.start < 0 or not 0 <= args.audio <= 31:
        p.error('Need a readable source, nonnegative start and audio 0..31')
    if args.encoder != 'software' and not Path(args.device).exists():
        p.error(f'VAAPI device unavailable: {args.device}; run on the Intel host/container with GPU access')
    for hardware in ([False, True] if args.encoder == 'both' else [args.encoder == 'vaapi']):
        create_case(args.source.resolve(), args.output.resolve(), hardware, args.device,
                    args.profile == 'tv', args.seconds, args.start, args.audio)


if __name__ == '__main__':
    main()

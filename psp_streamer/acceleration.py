"""Optional video encoding only. Never alter FLV clocks or audio options."""
import json
import logging
import os
from pathlib import Path
import re
import subprocess
import tempfile
import threading
import time

MODES = {"software", "auto", "vaapi", "nvenc"}


def hardware_command(command, backend, device):
    """Keep CPU subtitle/scale filters; upload only their final NV12 frame."""
    if backend == "software" or "libx264" not in command:
        return list(command)
    result = []
    i = 0
    while i < len(command):
        key = command[i]
        if key == "-level:v" and backend == "vaapi":
            # VA-API takes integer level_idc, unlike x264's "3.0" spelling.
            result += [key, "30"]
            i += 2
            continue
        if key in {"-preset", "-tune", "-x264-params", "-profile:v"}:
            i += 2
            continue
        if key == "-c:v":
            result += [key, "h264_" + backend]
            i += 2
            continue
        if key in {"-vf", "-filter_complex"} and backend == "vaapi":
            value = command[i+1]
            tail = ",format=nv12,hwupload"
            value = value[:-3] + tail + "[v]" if key == "-filter_complex" else value + tail
            result += [key, value]
            i += 2
            continue
        result.append(key)
        i += 1
    # Output options must precede the output URL, not the input.
    common = ["-bf", "0", "-g", "64", "-refs", "1", "-aud", "1"]
    if backend == "vaapi":
        result[1:1] = ["-vaapi_device", device]
        common += ["-profile:v", "constrained_baseline", "-coder", "cavlc", "-rc_mode", "VBR", "-async_depth", "1"]
    else:
        common += ["-profile:v", "baseline", "-preset", "p4", "-tune", "ll",
                   "-rc", "vbr", "-rc-lookahead", "0", "-zerolatency", "1"]
    result[-1:-1] = common
    return result


class Acceleration:
    def __init__(self):
        self.lock = threading.RLock()
        directory = os.environ.get("PSP_STREAMER_SETTINGS_DIR", "")
        self.path = Path(directory) / "acceleration.json" if directory else None
        self.mode = os.environ.get("PSP_STREAMER_ACCELERATION", "software")
        self.device = os.environ.get("PSP_STREAMER_VAAPI_DEVICE", "/dev/dri/renderD128")
        if self.path and self.path.exists():
            saved = json.loads(self.path.read_text())
            self.mode, self.device = saved["mode"], saved["device"]
        self.validate(self.mode, self.device)
        self.cache = {}
        self.active = "not tested"
        self.detail = "Hardware is checked on the next video request."

    @staticmethod
    def validate(mode, device):
        if not isinstance(mode, str) or mode not in MODES:
            raise ValueError("Unknown acceleration mode")
        if not isinstance(device, str) or not re.fullmatch(r"/dev/dri/renderD[0-9]{1,5}", device):
            raise ValueError("Device must be /dev/dri/renderD<number>")

    def status(self):
        with self.lock:
            return {"mode": self.mode, "device": self.device, "editable": self.path is not None,
                    "active": self.active, "detail": self.detail,
                    "devices": sorted(str(p) for p in Path('/dev/dri').glob('renderD*'))}

    def change(self, mode, device):
        self.validate(mode, device)
        if self.path is None:
            raise ValueError("Acceleration is managed by Home Assistant / the environment")
        with self.lock:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            fd, temporary = tempfile.mkstemp(prefix='.acceleration-', dir=self.path.parent)
            try:
                with os.fdopen(fd, 'w') as output:
                    json.dump({"mode": mode, "device": device}, output)
                    output.flush()
                    os.fsync(output.fileno())
                os.replace(temporary, self.path)
            finally:
                if os.path.exists(temporary):
                    os.unlink(temporary)
            self.mode, self.device = mode, device
            self.cache.clear()
            self.active, self.detail = "not tested", "Applies to the next video; current streams are unchanged."

    @staticmethod
    def probe(backend, device, tv, fps):
        width, height = (720, 480) if tv else (480, 272)
        with tempfile.TemporaryDirectory(prefix='psp-encoder-') as directory:
            output = str(Path(directory) / 'probe.flv')
            command = ['ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i',
                       f'color=size={width}x{height}:rate={fps}', '-vf', 'format=yuv420p',
                       '-frames:v', '8', '-an', '-c:v', 'libx264', '-level:v', '3.0',
                       '-b:v', '850k' if tv else '600k', '-maxrate', '950k' if tv else '700k',
                       '-bufsize', '1200k' if tv else '900k', '-f', 'flv', output]
            try:
                run = subprocess.run(hardware_command(command, backend, device),
                                     capture_output=True, timeout=15)
                if run.returncode:
                    return False, run.stderr.decode(errors='replace')[-1200:].strip()
                check = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v:0',
                                        '-show_streams', '-show_packets', '-of', 'json', output],
                                       capture_output=True, timeout=5, check=True)
                data = json.loads(check.stdout)
                stream = data['streams'][0]
                packets = data.get('packets', [])
                valid = (stream.get('codec_name') == 'h264' and
                         stream.get('profile') in {'Baseline', 'Constrained Baseline'} and
                         stream.get('level') == 30 and stream.get('has_b_frames') == 0 and
                         stream.get('pix_fmt') == 'yuv420p' and
                         (stream.get('width'), stream.get('height')) == (width, height) and
                         len(packets) == 8 and all(p.get('pts') == p.get('dts') and 'pts' in p for p in packets) and
                         all(a['pts'] < b['pts'] for a, b in zip(packets, packets[1:])))
                return valid, "PSP Baseline/PTS check passed." if valid else "Encoder output failed PSP Baseline/PTS check."
            except (OSError, subprocess.SubprocessError, ValueError, KeyError, IndexError) as error:
                return False, str(error)

    def prepare(self, command, tv=False, fps="20"):
        if 'libx264' not in command:
            return command, 'software', False
        with self.lock:
            mode, device = self.mode, self.device
            if mode == 'software':
                self.active, self.detail = 'software', 'Hardware acceleration disabled.'
                return command, 'software', False
            candidates = ('vaapi', 'nvenc') if mode == 'auto' else (mode,)
            failures = []
            for backend in candidates:
                key = (backend, device, tv, fps)
                cached = self.cache.get(key)
                if cached is None or time.monotonic()-cached[0] > 300:
                    ok, detail = self.probe(backend, device, tv, fps)
                    self.cache[key] = (time.monotonic(), ok, detail)
                _, ok, detail = self.cache[key]
                if ok:
                    self.active, self.detail = backend, detail
                    return hardware_command(command, backend, device), backend, mode == 'auto'
                failures.append(backend + ': ' + detail)
            self.detail = '\n'.join(failures)
            self.active = 'software' if mode == 'auto' else 'unavailable'
            logging.warning('Video acceleration unavailable: %s', self.detail)
            if mode != 'auto':
                raise ValueError('Hardware encoder unavailable; see Server settings for details or select Software/Auto.')
            return command, 'software', False

    def failed_start(self, fallback=True):
        with self.lock:
            self.active = 'software' if fallback else 'unavailable'
            self.detail = ('Hardware produced no stream; restarted with software before sending media.' if fallback else
                           'Hardware produced no stream. Select Software/Auto or check the GPU setup.')
            self.cache.clear()

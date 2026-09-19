"""Persistent, single-worker conversion queue. Downloads are immutable and resumable.

Only server-issued job IDs address files; neither API accepts filesystem paths.
The PSP commits a download only after all manifest files have arrived.
"""
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import threading
import time
import uuid

MAX_FILE = 0xFFFFFFFF  # FAT32 file limit
MAX_JOBS = 128


class OfflineQueue:
    def __init__(self, root, library, command_builder, parse_cues, slots):
        self.root = Path(root)
        self.library, self.command_builder, self.parse_cues, self.slots = library, command_builder, parse_cues, slots
        self.lock = threading.RLock()
        self.wake = threading.Event()
        self.stopping = threading.Event()
        self.jobs = {}
        self.process = None
        self.thread = None
        if self.root.exists():
            for path in sorted(self.root.glob('*/job.json')):
                try:
                    job = json.loads(path.read_text())
                    if not re.fullmatch('[0-9a-f]{32}', job['job']) or path.parent.name != job['job']:
                        continue
                    if job['state'] in ('encoding', 'queued'):
                        job['state'] = 'queued'
                    job['created'] = float(job.get('created', 0))
                    self.jobs[job['job']] = job
                except (OSError, ValueError, KeyError, TypeError):
                    continue
            self.jobs = dict(sorted(self.jobs.items(), key=lambda item: item[1].get('created', 0)))

    def start(self):
        with self.lock:
            if self.thread is None:
                self.root.mkdir(parents=True, exist_ok=True)
                self.thread = threading.Thread(target=self._worker, daemon=True, name='offline-converter')
                self.thread.start()
        self.wake.set()

    def close(self):
        self.stopping.set()
        self.wake.set()
        with self.lock:
            if self.process and self.process.poll() is None:
                self.process.terminate()
        if self.thread:
            self.thread.join(timeout=10)
            if self.thread.is_alive():
                with self.lock:
                    if self.process and self.process.poll() is None:
                        self.process.kill()
                self.thread.join(timeout=5)

    def _save(self, job):
        folder = self.root / job['job']
        folder.mkdir(exist_ok=True)
        temporary = folder / 'job.json.tmp'
        temporary.write_text(json.dumps(job, ensure_ascii=False))
        temporary.replace(folder / 'job.json')

    def preferences(self, values=None):
        with self.lock:
            path = self.root / 'preferences.json'
            if values is not None:
                allowed = {'audio_quality': {'96k', '128k', '160k', 'v6', 'v5', 'v4', 'v3'},
                           'video_fps': {'20', '24000/1001'}, 'profile': {'normal', 'low', 'tv'}}
                clean = {}
                for key, value in values.items():
                    if key in allowed and isinstance(value, str) and value in allowed[key]:
                        clean[key] = value
                    elif key in {'audio', 'subtitle'} and isinstance(value, str) and len(value) <= 512:
                        clean[key] = value
                    else:
                        raise ValueError('Invalid preference')
                temporary = path.with_suffix('.tmp')
                self.root.mkdir(parents=True, exist_ok=True)
                temporary.write_text(json.dumps(clean, ensure_ascii=False), encoding='utf-8')
                temporary.replace(path)
                return clean
            try:
                return json.loads(path.read_text(encoding='utf-8'))
            except (OSError, ValueError):
                return {}

    def list(self):
        self.start()
        with self.lock:
            return [dict(job) for job in self.jobs.values()]

    def get(self, key):
        with self.lock:
            if key not in self.jobs:
                raise ValueError('Unknown download job')
            return dict(self.jobs[key])

    def add(self, options):
        if not isinstance(options.get('id'), str) or len(options['id']) > 4096:
            raise ValueError('Invalid media identifier')
        _, source = self.library.decode(str(options.get('id', '')))
        from .server import VIDEO_EXTENSIONS, AUDIO_EXTENSIONS
        music = source.suffix.lower() in AUDIO_EXTENSIONS
        if source.suffix.lower() not in VIDEO_EXTENSIONS | AUDIO_EXTENSIONS:
            raise ValueError('Offline downloads require a video or music file')
        clean = {'id': str(options['id']), 'audio': int(options.get('audio', 0)),
                 'subtitle': int(options.get('subtitle', -1)),
                 'audio_quality': str(options.get('audio_quality', '160k')),
                 'video_fps': str(options.get('video_fps', '20')),
                 'profile': str(options.get('profile', 'normal'))}
        if (not 0 <= clean['audio'] <= 31 or not -1 <= clean['subtitle'] <= 31 or
                clean['audio_quality'] not in {'96k', '128k', '160k', 'v6', 'v5', 'v4', 'v3'} or
                clean['video_fps'] not in {'20', '24000/1001'} or clean['profile'] not in {'normal', 'low', 'tv'}):
            raise ValueError('Invalid conversion options')
        if music:
            clean.update(audio=0, subtitle=-1, profile='normal')
        # FAT-safe leaf names. Unique job directories keep variants separate.
        name = re.sub(r'[\x00-\x1f<>:"/\\|?*]', '_', source.stem).strip(' .') or 'Video'
        while len(name.encode('utf-8')) > 110:
            name = name[:-1]
        self.start()
        with self.lock:
            if len(self.jobs) >= MAX_JOBS:
                raise ValueError('Queue full; remove old server jobs first')
            job = dict(clean, job=uuid.uuid4().hex, kind='audio' if music else 'video',
                       name=name + ('.mp3' if music else '.flv'), state='queued',
                       progress=0, bytes=0, duration=0, created=time.time(), error='', files=[])
            self.jobs[job['job']] = job
            self._save(job)
        self.wake.set()
        return dict(job)

    def cancel(self, key):
        with self.lock:
            job = self.jobs.get(key)
            if not job:
                raise ValueError('Unknown download job')
            active = job['state'] == 'encoding'
            job['state'] = 'cancelled'
            self._save(job)
            if active and self.process and self.process.poll() is None:
                self.process.terminate()
        return self.get(key)

    def delete(self, key):
        with self.lock:
            job = self.jobs.get(key)
            if not job or job['state'] in {'queued', 'encoding'}:
                raise ValueError('Cancel the job and wait for the worker before deleting')
            if getattr(self, 'active_key', None) == key:
                raise ValueError('Worker is still stopping; try again shortly')
            shutil.rmtree(self.root / key)
            del self.jobs[key]

    def file(self, key, number):
        job = self.get(key)
        if job['state'] != 'ready' or not 0 <= number < len(job['files']):
            raise ValueError('Download is not ready')
        entry = job['files'][number]
        path = self.root / key / entry['name']
        if path.parent != self.root / key or not path.is_file():
            raise ValueError('Download file unavailable')
        return path, entry

    def _run(self, command, job, **kwargs):
        with self.lock:
            if self.stopping.is_set() or job['state'] == 'cancelled':
                raise ValueError('Cancelled')
            self.process = subprocess.Popen(command, **kwargs)
            return self.process

    def _capture(self, command, job, timeout=180):
        process = self._run(command, job, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        try:
            output, _ = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            raise ValueError('Media inspection/subtitle extraction timed out')
        if process.returncode:
            raise ValueError('Media inspection/subtitle extraction failed')
        return output

    def _subtitles(self, job, source, probe, folder):
        from .plex_media import RemoteSource
        """OVL1: uint32 JSON length, compact cue JSON, then indexed PGS sprites."""
        track = job['subtitle']
        payload, sprites, burn = {'t': 'text', 'c': []}, [], -1
        streams = [s for s in probe['streams'] if s.get('codec_type') == 'subtitle']
        if track >= len(streams):
            raise ValueError('Selected subtitle track does not exist')
        if track >= 0:
            from .server import TEXT_SUBTITLE_CODECS, BITMAP_SUBTITLE_CODECS
            codec = streams[track]['codec_name']
            if codec in TEXT_SUBTITLE_CODECS:
                data = self._capture(['ffmpeg', '-v', 'error', '-i', str(source), '-map',
                                      f'0:s:{track}', '-f', 'srt', 'pipe:1'], job)
                payload['c'] = self.parse_cues(data.decode('utf-8'), 1000)
            elif codec == 'hdmv_pgs_subtitle' and job['profile'] != 'tv' and (isinstance(source, RemoteSource) or source.suffix.lower() == '.mkv'):
                from .pgs import parse_pgs
                sup = folder / 'extract.sup'
                if isinstance(source, RemoteSource):
                    self._capture(['ffmpeg', '-v', 'error', '-i', str(source), '-map', f'0:s:{track}',
                                   '-c:s', 'copy', '-f', 'sup', str(sup)], job, 600)
                else:
                    tracks = json.loads(self._capture(['mkvmerge', '-J', str(source)], job))['tracks']
                    selected = [t for t in tracks if t.get('type') == 'subtitles'][track]
                    self._capture(['mkvextract', 'tracks', str(source), f"{selected['id']}:{sup}"], job, 600)
                cues = parse_pgs(sup.read_bytes())
                sup.unlink()
                if len(cues) > 960 or any(len(c.palette) + len(c.pixels) > 512 * 1024 for c in cues):
                    burn = track  # keep existing bounded PSP overlay limits
                else:
                    payload = {'t': 'pgs', 'c': [[round(c.start*1000), round(c.end*1000), c.x, c.y,
                               c.width, c.height, c.canvas_width, c.canvas_height] for c in cues]}
                    sprites = [c.palette + c.pixels for c in cues]
            elif codec in BITMAP_SUBTITLE_CODECS:
                burn = track  # existing native-TV bitmap policy
            else:
                raise ValueError('Unsupported subtitle codec')
        data = json.dumps(payload, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
        with (folder / 'subtitles.ovl').open('wb') as output:
            output.write(b'OVL1' + struct.pack('<I', len(data)) + data)
            for sprite in sprites:
                output.write(sprite)
        return burn

    def _convert(self, job):
        _, source = self.library.decode(job['id'])
        folder = self.root / job['job']
        probe = json.loads(self._capture(['ffprobe', '-v', 'error', '-show_streams', '-show_format',
                                         '-of', 'json', str(source)], job))
        music = job.get('kind') == 'audio'
        if not music and not any(s.get('codec_type') == 'video' for s in probe['streams']):
            raise ValueError('No video stream')
        audio = [s for s in probe['streams'] if s.get('codec_type') == 'audio']
        if music and not audio:
            raise ValueError('No audio stream')
        if audio and job['audio'] >= len(audio):
            raise ValueError('Selected audio track does not exist')
        job['audio_label'] = audio[job['audio']].get('tags', {}).get('language', 'und') if audio else 'none'
        subtitles = [s for s in probe['streams'] if s.get('codec_type') == 'subtitle']
        job['subtitle_label'] = (subtitles[job['subtitle']].get('tags', {}).get('language', 'und')
                                 if 0 <= job['subtitle'] < len(subtitles) else 'off')
        job['duration'] = float(probe.get('format', {}).get('duration', 0))
        if music:
            from .radio import display_text
            tags = {k.lower(): v for stream in audio for k, v in stream.get('tags', {}).items()}
            tags.update({k.lower(): v for k, v in probe.get('format', {}).get('tags', {}).items()})
            job.update(title=display_text(tags.get('title') or source.stem),
                       artist=display_text(tags.get('artist') or tags.get('album_artist')))
        if shutil.disk_usage(folder).free < 32 * 1024 * 1024:
            raise ValueError('Insufficient server disk space')
        burn = self._subtitles(job, source, probe, folder)
        bitmap = burn >= 0
        command = self.command_builder(source, job['audio'], 'mp3' if music else 'flv', job['profile'] == 'low',
                                      burn, job['audio_quality'], None, 0, bitmap, job['profile'] == 'tv', job['video_fps'])
        if '-re' in command:
            command.remove('-re')
        if '-readrate_initial_burst' in command:
            i = command.index('-readrate_initial_burst')
            del command[i:i+2]
        temporary = folder / 'video.part'
        command[-1] = str(temporary)
        command[1:1] = ['-y', '-progress', 'pipe:1', '-nostats']
        process = self._run(command, job, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        started = time.monotonic()
        for line in process.stdout:
            if line.startswith('out_time_us='):
                try:
                    seconds = int(line.split('=', 1)[1]) / 1000000
                except ValueError:
                    continue
                with self.lock:
                    job['progress'] = min(99, round(seconds * 100 / job['duration'])) if job['duration'] else 0
                    job['bytes'] = temporary.stat().st_size if temporary.exists() else 0
                    job['eta'] = max(0, round((job['duration']-seconds)*(time.monotonic()-started)/seconds)) if seconds > 0 else None
                if job['bytes'] > MAX_FILE or self.stopping.is_set() or job['state'] == 'cancelled':
                    process.terminate()
        process.stdout.close()
        if process.wait() or job['state'] == 'cancelled' or self.stopping.is_set():
            raise ValueError('Conversion failed/cancelled (check space and FAT32 size limit)')
        temporary.replace(folder / job['name'])
        # Compact seek index: timestamp and byte position of each AVC keyframe.
        with (folder / job['name']).open('rb') as video, (folder / 'seek.idx').open('wb') as index:
            if not music and video.read(3) != b'FLV':
                raise ValueError('Invalid conversion output')
            video.seek(13)
            while not music:
                if self.stopping.is_set() or job['state'] == 'cancelled':
                    raise ValueError('Cancelled')
                offset = video.tell()
                tag = video.read(11)
                if not tag:
                    break
                if len(tag) != 11:
                    raise ValueError('Truncated FLV')
                size = int.from_bytes(tag[1:4], 'big')
                body = video.read(min(2, size))
                if tag[0] == 9 and body == b'\x17\x01':
                    pts = int.from_bytes(tag[4:7], 'big') | tag[7] << 24
                    index.write(struct.pack('<II', pts, offset))
                video.seek(size - len(body) + 4, 1)
        entries = []
        for name in (job['name'], 'subtitles.ovl', 'seek.idx'):
            path = folder / name
            size = path.stat().st_size
            if size > MAX_FILE:
                raise ValueError('File exceeds FAT32 limit')
            digest = hashlib.sha256()
            with path.open('rb') as data:
                for block in iter(lambda: data.read(1024*1024), b''):
                    if self.stopping.is_set() or job['state'] == 'cancelled':
                        raise ValueError('Cancelled')
                    digest.update(block)
            entries.append({'name': name, 'size': size, 'sha256': digest.hexdigest()})
        with self.lock:
            if job['state'] == 'cancelled' or self.stopping.is_set():
                return
            job.update(state='ready', progress=100, bytes=sum(e['size'] for e in entries), files=entries)

    def _worker(self):
        while not self.stopping.is_set():
            with self.lock:
                job = next((j for j in self.jobs.values() if j['state'] == 'queued'), None)
            if job is None:
                self.wake.wait(1)
                self.wake.clear()
                continue
            if not self.slots.acquire(timeout=1):
                continue
            try:
                with self.lock:
                    if job['state'] != 'queued':
                        continue
                    self.active_key = job['job']
                    job['state'] = 'encoding'
                    self._save(job)
                self._convert(job)
            except Exception as exc:
                with self.lock:
                    if job['state'] != 'cancelled':
                        job['state'] = 'queued' if self.stopping.is_set() else 'error'
                        job['error'] = str(exc)[:200] if isinstance(exc, ValueError) else 'Conversion failed; inspect server media/storage'
            finally:
                with self.lock:
                    try:
                        self._save(job)
                    except OSError:
                        job.update(state='error', error='Could not persist job; check server storage')
                    self.process = None
                    self.active_key = None
                self.slots.release()

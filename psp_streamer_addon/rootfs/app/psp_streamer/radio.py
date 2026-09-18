"""Persistent, single-owner radio directory and bounded playlist resolution."""
import json
import os
from pathlib import Path
import re
import tempfile
import threading
import uuid
from urllib.parse import urljoin, urlsplit
from urllib.request import Request, HTTPRedirectHandler, build_opener

RADIO_PATH = ':radio:'


def display_text(value, limit=160):
    """Text for the PSP's compact, deliberately escape-free JSON reader."""
    text = ' '.join(str(value or '').replace('"', "'").replace('\\', '/').split())
    text = ''.join(c for c in text if ord(c) >= 32 and ord(c) != 127)
    return text.encode('utf-8')[:limit].decode('utf-8', errors='ignore')


def validate_url(value):
    if not isinstance(value, str) or not 1 <= len(value) <= 2048:
        raise ValueError('Use an HTTP(S) stream or M3U/PLS URL')
    if any(ord(c) <= 32 or ord(c) == 127 for c in value):
        raise ValueError('URL contains whitespace or control characters')
    parts = urlsplit(value)
    if parts.scheme not in ('http', 'https') or not parts.hostname or parts.fragment:
        raise ValueError('Only HTTP(S) radio URLs without fragments are supported')
    if parts.username or parts.password:
        raise ValueError('Credentials embedded in radio URLs are not supported')
    if parts.port is not None and not 1 <= parts.port <= 65535:
        raise ValueError('Invalid radio port')
    return value


class RadioDirectory:
    def __init__(self, directory):
        self.path = Path(directory) / 'radio.json'
        self.lock = threading.RLock()
        self.stations = []
        self.sessions = {}
        if self.path.exists():
            data = json.loads(self.path.read_text(encoding='utf-8'))
            if not isinstance(data, list) or len(data) > 128:
                raise ValueError('Invalid radio directory')
            for entry in data:
                self._validate(entry)
            if len({e['id'] for e in data}) != len(data):
                raise ValueError('Duplicate radio identifiers')
            self.stations = data

    @staticmethod
    def _validate(entry):
        if not isinstance(entry, dict) or not re.fullmatch(r'radio\.[0-9a-f]{32}', entry.get('id', '')):
            raise ValueError('Invalid radio identifier')
        name = entry.get('name')
        if not isinstance(name, str) or not name.strip() or len(name.encode('utf-8')) > 100 or any(ord(c) < 32 for c in name):
            raise ValueError('Station name must be 1–100 UTF-8 bytes without control characters')
        validate_url(entry.get('url'))

    def list(self):
        with self.lock:
            return [dict(s) for s in self.stations]

    def get(self, token):
        with self.lock:
            for station in self.stations:
                if station['id'] == token:
                    return dict(station)
        raise ValueError('Radio station is unavailable')

    def change(self, data):
        if not isinstance(data, dict):
            raise ValueError('Invalid station settings')
        with self.lock:
            token = data.get('id')
            if token:
                self.get(token)
            else:
                token = 'radio.' + uuid.uuid4().hex
            updated = [dict(s) for s in self.stations]
            if data.get('delete') is True:
                if not data.get('id'):
                    raise ValueError('Select a station to delete')
                updated = [s for s in updated if s['id'] != token]
            else:
                station = {'id': token, 'name': data.get('name'), 'url': data.get('url')}
                self._validate(station)
                station['name'] = station['name'].strip()
                position = next((i for i, s in enumerate(updated) if s['id'] == token), len(updated))
                if position == len(updated):
                    if len(updated) >= 128:
                        raise ValueError('Radio directory is limited to 128 stations')
                    updated.append(station)
                else:
                    updated[position] = station
            self.path.parent.mkdir(parents=True, exist_ok=True)
            fd, temporary = tempfile.mkstemp(prefix='.radio-', dir=self.path.parent)
            try:
                with os.fdopen(fd, 'w', encoding='utf-8') as output:
                    json.dump(updated, output, ensure_ascii=False, indent=2)
                    output.flush()
                    os.fsync(output.fileno())
                os.replace(temporary, self.path)
            finally:
                if os.path.exists(temporary):
                    os.unlink(temporary)
            self.stations = updated
            return {'ok': True, 'id': token}

    def browse(self, root):
        return {'root': root, 'path': RADIO_PATH, 'parent': '', 'folders': [],
                'videos': [{'id': s['id'], 'name': display_text(s['name']), 'kind': 'audio', 'live': True, 'bytes': 0}
                           for s in self.list()]}

    def begin(self, token):
        with self.lock:
            station = self.get(token)
            lease = uuid.uuid4().hex
            self.sessions[token] = {'lease': lease, 'station': display_text(station['name']), 'title': ''}
            return lease

    def update(self, token, lease, key, value):
        value = display_text(value)
        with self.lock:
            entry = self.sessions.get(token)
            if entry and entry['lease'] == lease and key in ('title', 'station'):
                entry[key] = value

    def status(self, token):
        with self.lock:
            station = self.get(token)
            entry = self.sessions.get(token)
            return {'radio_station': entry['station'] if entry else display_text(station['name']),
                    'radio_title': entry['title'] if entry else '', 'radio_active': bool(entry)}

    def end(self, token, lease):
        with self.lock:
            if self.sessions.get(token, {}).get('lease') == lease:
                del self.sessions[token]


class RadioRedirects(HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, msg, headers, url):
        validate_url(url)
        return super().redirect_request(request, fp, code, msg, headers, url)


def resolve_playlist(url):
    """Resolve named .m3u/.pls lists, never read an endless stream as a list.

    No HTML scraping, HLS, local-file protocols or arbitrary FFmpeg inputs.
    A list's first entry is its station; multi-station import is not implied.
    """
    opener = build_opener(RadioRedirects())
    for _ in range(4):
        validate_url(url)
        extension = Path(urlsplit(url).path).suffix.lower()
        if extension == '.m3u8':
            raise ValueError('HLS playlists are not supported; use a direct radio stream')
        if extension not in ('.m3u', '.pls'):
            return url
        try:
            with opener.open(Request(url, headers={'User-Agent': 'PSPStreamer/1.0'}), timeout=8) as reply:
                body = reply.read(65537)
                base = validate_url(reply.geturl())
        except OSError as exc:
            raise ValueError('Could not load radio playlist') from exc
        if len(body) > 65536:
            raise ValueError('Radio playlist exceeds 64 KiB')
        text = body.decode('utf-8-sig', errors='replace')
        if '#EXT-X-' in text:
            raise ValueError('HLS playlists are not supported')
        if extension == '.pls':
            matches = re.findall(r'^File(\d+)\s*=\s*(.+)$', text, re.M | re.I)
            candidates = [v.strip() for _, v in sorted(matches, key=lambda pair: int(pair[0]))]
        else:
            candidates = [line.strip() for line in text.splitlines() if line.strip() and not line.lstrip().startswith('#')]
        if not candidates:
            raise ValueError('Radio playlist has no stream URL')
        url = validate_url(urljoin(base, candidates[0]))
    raise ValueError('Radio playlist nesting is too deep')


def radio_command(url, quality, audio_command):
    command = audio_command(url, 0, 'mp3', audio_bitrate=quality)
    command[command.index('-loglevel') + 1] = 'verbose'
    # Input-only protocol/demux restrictions: prevent a URL from opening local
    # files or arbitrary nested formats. Preserve the existing PSP MP3 output.
    position = command.index('-i')
    command[position:position] = ['-nostdin', '-nostats', '-icy', '1', '-protocol_whitelist', 'http,https,tcp,tls',
        '-format_whitelist', 'mp3,aac,ogg,flac,wav', '-rw_timeout', '10000000',
        *(['-tls_verify', '1'] if urlsplit(url).scheme == 'https' else []),
        '-reconnect', '1', '-reconnect_streamed', '1',
        '-reconnect_at_eof', '1', '-reconnect_delay_max', '3']
    command[-1:-1] = ['-flush_packets', '1']
    return command


class IcyLogReader:
    """Consume FFmpeg's bounded verbose ICY events from the SAME audio input.

    libavformat/http.c update_metadata logs StreamTitle; the demuxer's initial
    metadata dump supplies icy-name. Never save or forward arbitrary log lines.
    This avoids a second receiver connection (and differently timed titles).
    """
    def __init__(self, directory, token, lease):
        self.directory, self.token, self.lease = directory, token, lease
        self.pending = b''
        self.discard = False

    def feed(self, data):
        for chunk in data.splitlines(keepends=True):
            self.pending += chunk
            if len(self.pending) > 8192:
                self.discard = True
                self.pending = b''
            if not chunk.endswith(b'\n'):
                continue
            if not self.discard:
                try:
                    text = self.pending.decode('utf-8')
                except UnicodeDecodeError:
                    text = self.pending.decode('cp1252', errors='replace')
                match = re.search(r'Metadata update for StreamTitle: (.*)', text)
                if match:
                    value = match[1].replace("\\'", "'").replace('\\\\', '\\')
                    self.directory.update(self.token, self.lease, 'title', value)
                else:
                    match = re.match(r'\s*icy-name\s*:\s*(.*)', text)
                    if match:
                        self.directory.update(self.token, self.lease, 'station', match[1])
            self.pending = b''
            self.discard = False

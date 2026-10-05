"""Bounded, expiring Xbox-only remote mailbox. Never touches PSP commands."""
import threading
import time
from .xbox_player import PROFILES


class XboxRemote:
    def __init__(self):
        self.lock = threading.Lock()
        self.sequence = 0
        self.command = {}
        self.expires = 0
        self.seen = 0
        self.playback = {}
        self.reported = 0

    def send(self, server, data):
        if not isinstance(data, dict):
            raise ValueError('Invalid Xbox command')
        action = data.get('action')
        if action not in ('play', 'pause', 'resume', 'stop', 'seek', 'next', 'previous'):
            raise ValueError('Unsupported Xbox action')
        clean = {'action': action}
        if action == 'play':
            size = data.get('xbox_size')
            if size is not None and (not isinstance(size, str) or size not in PROFILES):
                raise ValueError('Invalid Xbox video size')
            token = data.get('id')
            if not isinstance(token, str) or not token or len(token) >= 1536:
                raise ValueError('Invalid media identifier')
            if token.startswith('radio.'):
                server.radio.get(token)
            else:
                server.library.decode(token)
            clean.update(id=token, audio=max(0, min(31, int(data.get('audio', 0)))),
                         subtitle=max(-1, min(31, int(data.get('subtitle', -1)))),
                         start=max(0, min(604800, int(data.get('start', 0)))))
            if size is not None:
                clean['xbox_size'] = size
            bitrate = data.get('xbox_audio')
            if bitrate is not None:
                if bitrate not in ('128k','192k','256k','320k','384k'):
                    raise ValueError('Invalid Xbox MP2 bitrate')
                clean['xbox_audio'] = bitrate
        if action == 'seek':
            clean['seconds'] = max(0, min(604800, int(data.get('seconds', 0))))
        with self.lock:
            self.sequence += 1
            self.command = dict(clean, sequence=self.sequence)
            self.expires = time.monotonic() + 15
            return {'ok': True, 'sequence': self.sequence}

    def poll(self, after):
        with self.lock:
            self.seen = time.monotonic()
            return dict(self.command) if after < self.sequence and self.seen < self.expires else {}

    def report(self, data, token, name):
        with self.lock:
            self.playback = {key: data.get(key) for key in ('state', 'position', 'duration', 'kind')}
            self.playback.update(id=token, title=name)
            self.playback['live']=token.startswith('radio.')
            self.reported=time.monotonic()

    def snapshot(self):
        with self.lock:
            return dict(self.playback, online=bool(self.seen and time.monotonic()-self.seen < 12),
                        age=max(0,time.monotonic()-self.reported) if self.reported else 0)

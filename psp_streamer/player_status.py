"""Read-only playback telemetry, independent of the remote command mailbox."""
from collections import OrderedDict
from pathlib import Path
import threading
import time
import uuid


class PlayerStatus:
    def __init__(self, directory):
        path = Path(directory) / 'player-id.txt'
        path.parent.mkdir(parents=True, exist_ok=True)
        try:
            with path.open('x') as output:
                output.write(str(uuid.uuid4()))
        except FileExistsError:
            pass
        self.identity = str(uuid.UUID(path.read_text().strip()))
        self.lock = threading.Lock()
        self.tick = None
        self.current = {'state': 'idle'}
        self.metadata = OrderedDict()

    def remember(self, token, payload, kind):
        data = {'title': payload.get('name') or payload.get('title') or '',
                'artist': payload.get('artist', ''), 'album': payload.get('album', ''), 'kind': kind,
                'artwork': payload.get('artwork', {})}
        with self.lock:
            self.metadata[token] = data
            self.metadata.move_to_end(token)
            while len(self.metadata) > 128:
                self.metadata.popitem(last=False)

    def report(self, query):
        def value(key, default=''):
            return query.get(key, [default])[0]
        token = value('media') or value('plex') or value('radio')
        state = value('state', 'idle' if not token else 'buffering')
        if state not in {'idle', 'playing', 'paused', 'stopped', 'buffering'} or len(token) > 1024:
            return
        # Telemetry is best-effort: malformed telemetry never breaks controls.
        def number(key, limit):
            try:
                return max(0, min(limit, int(value(key, '0'))))
            except (ValueError, TypeError):
                return 0
        data = {'state': 'idle' if state == 'stopped' else state,
                'id': token, 'position': number('position', 86400000)/1000,
                'duration': number('duration', 86400000)/1000,
                'live': token.startswith('radio.')}
        if value('started') == '0' and state in {'playing', 'paused'}:
            data['state'] = 'buffering'
        with self.lock:
            self.tick = time.monotonic()
            self.current = data if data['state'] != 'idle' else {'state': 'idle'}

    def snapshot(self):
        with self.lock:
            age = None if self.tick is None else max(0, time.monotonic()-self.tick)
            data = dict(self.current)
            data.update(self.metadata.get(data.get('id'), {}))
            return {'api': 1, 'server_id': self.identity, 'online': age is not None and age < 45,
                    'age': age, **data}

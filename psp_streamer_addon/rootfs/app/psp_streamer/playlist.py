"""One durable, explicitly enabled queue shared by the web and PSP clients."""
import json
import os
from pathlib import Path
import tempfile
import threading


class Playlist:
    def __init__(self, directory):
        self.path = Path(directory)/'playlist.json'
        self.lock = threading.RLock()
        self.data = {'revision': 0, 'enabled': False, 'items': []}
        self.gaps = {}
        if self.path.exists():
            self.data = json.loads(self.path.read_text())
            self.gaps = self.data.pop('gaps', {})

    def snapshot(self):
        with self.lock:
            return {**self.data, 'items': [dict(row) for row in self.data['items']]}

    def change(self, request, resolve):
        # Providers may need network I/O. Never hold the queue lock during
        # resolution: a bulk add must not stall the PSP's end-of-track lookup.
        added = None
        if request.get('action') == 'add':
            with self.lock:
                if request.get('revision') != self.data['revision']:
                    raise ValueError('Playlist changed; refresh and try again')
            values = request.get('items')
            if not isinstance(values,list) or not 1<=len(values)<=128:
                raise ValueError('Select 1 to 128 playlist items')
            if any(not isinstance(item,dict) for item in values):
                raise ValueError('Invalid playlist item')
            added = [resolve(item) for item in values]
        with self.lock:
            if request.get('revision') != self.data['revision']:
                raise ValueError('Playlist changed; refresh and try again')
            draft = self.snapshot()
            gaps = dict(self.gaps)
            rows = draft['items']
            action = request.get('action')
            token = request.get('id')
            removed = None
            if action == 'add':
                for row in added:
                    if not any(r['id'] == row['id'] for r in rows):
                        rows.append(row)
                if len(rows) > 128:
                    raise ValueError('Playlist limit: 128 items')
            elif action in {'remove', 'move'}:
                index = next((i for i,r in enumerate(rows) if r['id'] == token), -1)
                if index < 0:
                    raise ValueError('Playlist item is unavailable')
                row = rows.pop(index)
                gaps = {key: value-int(value>index) for key,value in gaps.items()}
                if action == 'move':
                    position = request.get('position')
                    if type(position) is not int or not 0 <= position <= len(rows):
                        raise ValueError('Invalid playlist position')
                    rows.insert(position, row)
                    gaps = {key: value+int(value>=position) for key,value in gaps.items()}
                else:
                    removed = (token, index)
            elif action == 'clear':
                gaps.update({row['id']:0 for row in rows})
                gaps = {key:0 for key in gaps}
                rows.clear()
            elif action == 'enabled':
                if type(request.get('enabled')) is not bool:
                    raise ValueError('Invalid playlist mode')
                draft['enabled'] = request['enabled']
            else:
                raise ValueError('Invalid playlist action')
            draft['revision'] += 1
            if removed:gaps[removed[0]] = removed[1]
            for row in rows:gaps.pop(row['id'],None)
            while len(gaps)>128:gaps.pop(next(iter(gaps)))
            fd, temporary = tempfile.mkstemp(prefix='.playlist-', dir=self.path.parent)
            try:
                with os.fdopen(fd, 'w') as output:
                    json.dump({**draft,'gaps':gaps}, output, ensure_ascii=False)
                    output.flush();os.fsync(output.fileno())
                os.replace(temporary, self.path)
            finally:
                if os.path.exists(temporary):os.unlink(temporary)
            self.data = draft
            self.gaps = gaps
            return self.snapshot()

    def next(self, token, previous=False):
        with self.lock:
            if not self.data['enabled']:return None
            rows = self.data['items']
            index = next((i for i,r in enumerate(rows) if r['id'] == token), -1)
            if index >= 0:index += -1 if previous else 1
            elif token in self.gaps:index = self.gaps[token] - int(previous)
            else:return None
            return dict(rows[index]) if 0 <= index < len(rows) else {}

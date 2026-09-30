"""Opt-in server-side episode reserve; source media/manual downloads are sacred.

The only eviction authority is a cache_owner marker written by this worker.
Rules, track languages and limits survive container updates in the state volume.
"""
import copy
import json
import os
from pathlib import Path
import tempfile
import threading
from urllib.parse import urlencode
import uuid


class EpisodeCache:
    def __init__(self, server, directory):
        self.server = server
        self.path = Path(directory) / 'episode-cache.json'
        self.lock = threading.RLock()
        self.stop = threading.Event()
        self.wake = threading.Event()
        self.thread = None
        self.revision = 0
        self.errors = {}
        self.data = dict(enabled=False, limit_mib=4096, rules=[])
        if self.path.exists():
            self.data.update(json.loads(self.path.read_text(encoding='utf-8')))
        self.server.offline.cache_guard = self.guard

    def start(self):
        with self.lock:
            if self.thread is None:
                self.thread = threading.Thread(target=self._worker, daemon=True, name='episode-reserve')
                self.thread.start()

    def close(self):
        self.stop.set()
        self.wake.set()
        if self.thread:
            self.thread.join(timeout=10)

    def snapshot(self):
        with self.lock:
            result = copy.deepcopy(self.data)
            result['errors'] = dict(self.errors)
        result['bytes'] = self.usage()
        return result

    def save(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        fd, temporary = tempfile.mkstemp(prefix='.episode-cache-', dir=self.path.parent)
        try:
            with os.fdopen(fd, 'w', encoding='utf-8') as handle:
                json.dump(self.data, handle, ensure_ascii=False)
                handle.flush()
                os.fsync(handle.fileno())
            os.replace(temporary, self.path)
        finally:
            if os.path.exists(temporary):
                os.unlink(temporary)

    def configure(self, request, metadata=None):
        action = request.get('action')
        rule = None
        if action == 'add':
            token = request.get('id')
            if not isinstance(token, str) or len(token) > 511 or not metadata or metadata.get('kind') != 'video':
                raise ValueError('Choose a video episode first')
            scope = self.server.series_preferences.context(self.server, token)
            if not scope:
                raise ValueError('Episode reserve supports series and file folders')
            count = request.get('count', 3)
            if type(count) is not int or not 1 <= count <= 20:
                raise ValueError('Choose between 1 and 20 episodes')
            options = self.server.offline.prepare(request)
            def preference(field, rows):
                n = options[field]
                if field == 'subtitle' and n == -1:
                    return None
                row = next((r for r in rows if int(r['n']) == n), None)
                if row is None:
                    raise ValueError('Track is unavailable')
                return dict(language=row.get('l', 'und'), title=row.get('t', ''))
            rule = dict(key=uuid.uuid4().hex, id=token, scope=scope, count=count,
                        name=metadata.get('name', '')[:200],
                        audio=preference('audio', metadata['a']),
                        subtitle=preference('subtitle', metadata['s']),
                        **{key: options[key] for key in ('profile', 'audio_quality', 'video_fps')})
        with self.lock:
            draft = copy.deepcopy(self.data)
            if action == 'settings':
                if type(request.get('enabled')) is not bool or type(request.get('limit_mib')) is not int or not 128 <= request['limit_mib'] <= 1048576:
                    raise ValueError('Use a cache limit of 128–1048576 MiB')
                draft.update(enabled=request['enabled'], limit_mib=request['limit_mib'])
            elif action == 'add':
                draft['rules'] = [r for r in draft['rules'] if r['scope'] != rule['scope']] + [rule]
                if len(draft['rules']) > 16:
                    raise ValueError('Use up to 16 episode reserve rules')
            elif action == 'remove':
                draft['rules'] = [r for r in draft['rules'] if r['key'] != request.get('key')]
            elif action != 'refresh':
                raise ValueError('Invalid episode reserve action')
            old = self.data
            self.data = draft
            try:
                self.save()
            except OSError:
                self.data = old
                raise
            self.revision += 1
            self.errors.clear()
        self.start()
        self.wake.set()
        return self.snapshot()

    def usage(self):
        queue = self.server.offline
        with queue.lock:
            return sum(self._size(job) for job in queue.jobs.values() if job.get('cache_owner'))

    def _folder(self, job):
        root = self.server.offline.root.resolve()
        folder = self.server.offline.root / job['job']
        if folder.is_symlink() or folder.resolve().parent != root:
            raise ValueError('Unsafe cache directory')
        return folder

    def _size(self, job):
        folder = self._folder(job)
        total = 0
        if folder.exists():
            for path in folder.iterdir():
                try:
                    if path.is_file() and not path.is_symlink():
                        total += path.stat().st_size
                except FileNotFoundError:
                    pass  # The converter atomically renames its temporary file.
        return total

    def guard(self, job):
        # Called while converting too, not just after an oversized file is ready.
        if not job.get('cache_owner'):
            return
        # No cache lock here: queue -> cache lock inversion would deadlock.
        if self.usage() > self.data['limit_mib'] * 1048576:
            raise ValueError('Episode reserve storage limit reached; raise limit or reduce reserve')

    def candidates(self, rule):
        server, token = self.server, rule['id']
        if server.series_preferences.context(server, token) != rule['scope']:
            raise ValueError('Media source/account changed; recreate this reserve rule')
        for name in ('plex', 'jellyfin'):
            if not token.startswith(name + '.'):
                continue
            provider = getattr(server, name)
            anchor = provider.metadata(token)
            floor = (int(anchor.get('parentIndex' if name == 'plex' else 'ParentIndexNumber') or 0),
                     int(anchor.get('index' if name == 'plex' else 'IndexNumber') or 0))
            rows = []
            for offset in range(0, 10000, 100):
                if self.stop.is_set():
                    return []
                if name == 'plex':
                    series = str(anchor['grandparentRatingKey'])
                    if not series.isdigit():
                        raise ValueError('Invalid series')
                    query = urlencode({'X-Plex-Container-Start': offset, 'X-Plex-Container-Size': 100})
                    page = provider.request('/library/metadata/' + series + '/allLeaves?' + query).get('MediaContainer', {}).get('Metadata', [])
                else:
                    from .jellyfin import identifier
                    query = urlencode(dict(UserId=provider.config['user'], ParentId=identifier(anchor['SeriesId']),
                                           Recursive='true', IncludeItemTypes='Episode', EnableUserData='true',
                                           StartIndex=offset, Limit=100, SortBy='ParentIndexNumber,IndexNumber', SortOrder='Ascending'))
                    page = provider.request('/Items?' + query).get('Items', [])
                for row in page:
                    order = (int(row.get('parentIndex' if name == 'plex' else 'ParentIndexNumber') or 0),
                             int(row.get('index' if name == 'plex' else 'IndexNumber') or 0))
                    watched = bool(row.get('viewCount', 0)) if name == 'plex' else bool((row.get('UserData') or {}).get('Played'))
                    if order >= floor and not watched:
                        key = str(row['ratingKey']) if name == 'plex' else row['Id']
                        rows.append((order, provider.token(key)))
                if len(page) < 100:
                    return list(dict.fromkeys(token for _, token in sorted(rows)))[:rule['count']]
            raise ValueError('Series exceeds the 10000 episode inspection limit')
        # Mounted SMB/files: the existing natural folder order, with our own
        # persisted completion flags. Unknown files count as unwatched.
        with server.comfort.lock:
            completed = set(server.comfort.data.get('completed', []))
        result, seen = [], set()
        for _ in range(10000):
            if self.stop.is_set() or token in seen:
                break
            seen.add(token)
            if token not in completed:
                result.append(token)
            if len(result) >= rule['count']:
                break
            token = server.library.next_media(token).get('id')
            if not token:
                break
        return result

    def options(self, rule, token):
        from .server import AppHandler
        # Reuse the exact metadata/sidecar mapping used by the web selector.
        handler = object.__new__(AppHandler)
        handler.server, handler.headers = self.server, {'X-PSP-Web': '1'}
        metadata = handler.metadata(token)
        result = dict(id=token, **{k: rule[k] for k in ('profile', 'audio_quality', 'video_fps')})
        for field, key in (('audio', 'a'), ('subtitle', 's')):
            preference = rule[field]
            if preference is None:
                result[field] = -1
            else:
                selected = self.server.series_preferences.match(metadata[key], preference, -2)
                if selected == -2:
                    raise ValueError('Preferred track missing; episode reserve paused')
                result[field] = selected
        return result

    def discard(self, job):
        queue = self.server.offline
        with queue.lock:
            current = queue.jobs.get(job['job'])
            if not current or not current.get('cache_owner'):
                return  # A manual request may have pinned it since our snapshot.
            self._folder(current)
            if current['state'] in ('queued', 'encoding'):
                queue.cancel(current['job'])
            if getattr(queue, 'active_key', None) != current['job']:
                queue.delete(current['job'])

    def refresh(self):
        with self.lock:
            settings, revision = copy.deepcopy(self.data), self.revision
        queue = self.server.offline
        rules = settings['rules'] if settings['enabled'] else []
        keys = {r['key'] for r in rules}
        with queue.lock:
            jobs = [dict(j) for j in queue.jobs.values()]
        for job in jobs:
            if job.get('cache_owner') and job['cache_owner'] not in keys:
                self.discard(job)
        for rule in rules:
            if self.stop.is_set():
                return
            try:
                tokens = self.candidates(rule)
                with self.lock:
                    if revision != self.revision:
                        return
                with queue.lock:
                    old = [dict(j) for j in queue.jobs.values() if j.get('cache_owner') == rule['key']]
                quality = ('profile', 'audio_quality', 'video_fps')
                for job in old:
                    if job['id'] not in tokens or any(job.get(k) != rule[k] for k in quality):
                        self.discard(job)
                for token in tokens:
                    if self.stop.is_set():
                        return
                    # Failed conversions are not retried endlessly in the background.
                    with queue.lock:
                        existing = next((j for j in queue.jobs.values() if j.get('cache_owner') == rule['key'] and j['id'] == token), None)
                    if existing:
                        if existing['state'] in ('error', 'cancelled'):
                            raise ValueError(existing.get('error') or 'Remove cancelled reserve job to retry')
                        continue
                    options = self.options(rule, token)
                    with self.lock:
                        if revision != self.revision or self.stop.is_set():
                            return
                        if self.usage() >= settings['limit_mib'] * 1048576:
                            raise ValueError('Episode reserve storage limit reached')
                        queue.add_many([options], cache_owner=rule['key'])
                with self.lock:
                    self.errors.pop(rule['key'], None)
            except (ValueError, OSError, KeyError, TypeError) as error:
                with self.lock:
                    self.errors[rule['key']] = str(error) if isinstance(error, ValueError) else 'Episode reserve unavailable; check source/storage'
        # On a reduced limit, evict newest/farthest reserve packages first.
        with queue.lock:
            owned = [dict(j) for j in queue.jobs.values() if j.get('cache_owner')]
        for job in reversed(owned):
            if self.usage() <= settings['limit_mib'] * 1048576:
                break
            with queue.lock:
                current = queue.jobs.get(job['job'])
                if not current or not current.get('cache_owner'):
                    continue
                if current['state'] in ('queued', 'encoding'):
                    queue.cancel(current['job'])
                if getattr(queue, 'active_key', None) != current['job']:
                    queue.clear_cache_files(current)
                    current.update(state='error', error='Episode reserve storage limit reached; remove this job to retry')
                    queue._save(current)

    def _worker(self):
        while not self.stop.is_set():
            self.wake.clear()
            try:
                self.refresh()
            except Exception:
                with self.lock:
                    self.errors['worker'] = 'Episode reserve unavailable; check storage'
            self.wake.wait(120)

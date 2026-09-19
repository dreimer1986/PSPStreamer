"""Plex catalogue adapter. Credentials never leave the server.

Media is read from explicitly mapped mounts, not Plex's transcoder. This keeps
the same subtitle extraction and offline conversion paths as filesystem media.
"""
import json
import hashlib
import os
from pathlib import Path, PurePosixPath
import random
import re
import tempfile
import threading
import time
import uuid
from urllib.parse import urlencode, urlsplit
from urllib.request import Request, HTTPRedirectHandler, build_opener
from urllib.error import URLError

from .radio import display_text


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None  # Never forward a Plex credential to another origin.


class Plex:
    def __init__(self, directory, roots):
        self.path = Path(directory) / 'plex.json'
        self.roots = roots
        self.lock = threading.RLock()
        self.config = dict(client=str(uuid.uuid4()), enabled=False, files=True,
                           radio=True, token='', account='', url='', mappings=[])
        if self.path.exists():
            self.config.update(json.loads(self.path.read_text(encoding='utf-8')))
        self.pin = None
        self.resources = []
        self.cache = {}
        self.parents = {}
        self.pending = {}
        self.last_report = {}
        self.report_condition = threading.Condition()
        self.report_thread = None
        self.closed = False
        self.report_error = ''

    def save(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        fd, name = tempfile.mkstemp(prefix='.plex-', dir=self.path.parent)
        try:
            with os.fdopen(fd, 'w', encoding='utf-8') as handle:
                json.dump(self.config, handle, ensure_ascii=False)
            os.replace(name, self.path)  # mkstemp's 0600 also protects tokens.
        finally:
            if os.path.exists(name):
                os.unlink(name)

    def public(self):
        with self.lock:
            return {k: self.config[k] for k in ('enabled', 'files', 'radio', 'url', 'mappings')} | {
                'linked': bool(self.config['account']), 'selected': bool(self.config['token']),
                'report_error': self.report_error}

    def request(self, path, *, cloud=False, method='GET', data=None, token=None, url=None):
        with self.lock:
            base = 'https://plex.tv' if cloud else (url or self.config['url'])
            credential = token if token is not None else self.config['account' if cloud else 'token']
            client = self.config['client']
        if not base:
            raise ValueError('Select a Plex server first')
        headers = {'Accept': 'application/json', 'X-Plex-Product': 'PSPStreamer',
                   'X-Plex-Version': '1.0', 'X-Plex-Client-Identifier': client,
                   'X-Plex-Device-Name': 'PSPStreamer', 'X-Plex-Platform': 'PSP',
                   'X-Plex-Provides': 'player', 'X-Plex-Token': credential}
        payload = urlencode(data).encode() if data is not None else None
        try:
            with build_opener(NoRedirect()).open(Request(base + path, data=payload,
                    headers=headers, method=method), timeout=8) as response:
                raw = response.read(4 * 1024 * 1024 + 1)
                if len(raw) > 4 * 1024 * 1024:
                    raise ValueError('Plex response exceeds the safe size limit')
                return json.loads(raw) if raw.strip() else {}
        except (URLError, TimeoutError, OSError, json.JSONDecodeError):
            # Do not echo upstream URLs, tokens or response bodies into logs/UI.
            raise ValueError('Plex request failed; check server, connection and authorization') from None

    def link(self):
        with self.lock:
            pin = self.request('/api/v2/pins', cloud=True, method='POST',
                               data={'strong': 'true'}, token='')
            self.pin = dict(id=int(pin['id']), code=str(pin['code']), expires=time.monotonic() + 300)
            self.save()
            return {'url': 'https://app.plex.tv/auth#?' + urlencode({
                'clientID': self.config['client'], 'code': pin['code'],
                'context[device][product]': 'PSPStreamer'})}

    def poll(self):
        with self.lock:
            if not self.pin or time.monotonic() >= self.pin['expires']:
                raise ValueError('Plex sign-in expired; start again')
            pin = self.request('/api/v2/pins/' + str(self.pin['id']) + '?' +
                               urlencode({'code': self.pin['code']}), cloud=True, token='')
            if pin.get('authToken'):
                self.config['account'] = pin['authToken']
                self.pin = None
                self.save()
                return {'linked': True}
            return {'linked': False}

    def servers(self):
        resources = self.request('/api/v2/resources?includeHttps=1&includeRelay=0', cloud=True)
        with self.lock:
            self.resources = [r for r in resources if 'server' in r.get('provides', '').split(',')]
            return [{'id': r['clientIdentifier'], 'name': r['name'],
                     'connections': [c['uri'] for c in r.get('connections', []) if not c.get('relay')]}
                    for r in self.resources]

    def select(self, server, url):
        with self.lock:
            resource = next((r for r in self.resources if r['clientIdentifier'] == server), None)
            if not resource or url not in [c['uri'] for c in resource.get('connections', [])]:
                raise ValueError('Refresh the Plex server list and select a listed connection')
            parts = urlsplit(url)
            if parts.scheme not in ('http', 'https') or not parts.hostname or parts.username or parts.password:
                raise ValueError('Invalid Plex connection')
            result = self.request('/identity', url=url, token=resource['accessToken'])
            if result.get('MediaContainer', {}).get('machineIdentifier') != server:
                raise ValueError('Plex server identity does not match')
            self.config.update(url=url.rstrip('/'), token=resource['accessToken'], server=server)
            self.cache.clear()
            self.parents.clear()
            with self.report_condition:
                self.pending.clear()
                self.last_report.clear()
            self.save()
            return self.public()

    def configure(self, data):
        mappings = data.get('mappings', [])
        if not isinstance(mappings, list) or len(mappings) > 32:
            raise ValueError('Use up to 32 Plex path mappings')
        checked = []
        for mapping in mappings:
            prefix = str(mapping.get('plex', '')).replace('\\', '/').rstrip('/')
            local = Path(str(mapping.get('local', ''))).resolve()
            if not prefix or not local.is_dir() or not any(local == r or r in local.parents for r in self.roots):
                raise ValueError('Map a Plex directory to a readable directory inside MEDIA_ROOTS')
            checked.append({'plex': prefix, 'local': str(local)})
        with self.lock:
            for key in ('enabled', 'files', 'radio'):
                if not isinstance(data.get(key), bool):
                    raise ValueError('Source switches must be booleans')
            if data['enabled'] and not self.config['token']:
                raise ValueError('Link and select a Plex server first')
            if not any(data[k] for k in ('enabled', 'files', 'radio')):
                raise ValueError('Keep at least one source enabled')
            self.config.update({k: data[k] for k in ('enabled', 'files', 'radio')}, mappings=checked)
            self.cache.clear()
            self.save()
            return self.public()

    def disconnect(self):
        with self.lock:
            self.config.update(enabled=False, files=True, token='', account='', url='')
            self.pin = None
            self.resources = []
            self.cache.clear()
            self.parents.clear()
            with self.report_condition:
                self.pending.clear()
                self.last_report.clear()
            self.save()
            return self.public()

    def require(self):
        if not self.config['enabled']:
            raise ValueError('Plex source is disabled')

    def split(self, token):
        self.require()
        match = re.fullmatch(r'plex\.([0-9]{1,20})(?:\.(s|p|m)([0-9]{1,20})\.([0-9]{1,10}))?\.([0-9a-f]{12})', token)
        if not match or match.group(5) != self.namespace():
            raise ValueError('Invalid Plex media identifier')
        return match.groups()[:4]

    def namespace(self):
        # A queued/offline ID from another Plex server must never resolve to
        # an unrelated item sharing the same numeric ratingKey.
        return hashlib.sha256(str(self.config.get('server') or self.config['url']).encode()).hexdigest()[:12]

    def token(self, identifier):
        return 'plex.' + identifier + '.' + self.namespace()

    def metadata(self, token):
        key = self.split(token)[0]
        now = time.monotonic()
        with self.lock:
            cached = self.cache.get(key)
            if cached and cached[0] > now:
                return cached[1]
        rows = self.request('/library/metadata/' + key).get('MediaContainer', {}).get('Metadata', [])
        if not rows:
            raise ValueError('Plex item is unavailable')
        with self.lock:
            if len(self.cache) >= 128:
                self.cache.clear()
            self.cache[key] = (now + 20, rows[0])
        return rows[0]

    def source(self, token):
        row = self.metadata(token)
        media = row.get('Media', [])
        if not media or len(media[0].get('Part', [])) != 1:
            raise ValueError('Plex item needs one original file; multipart media is not supported yet')
        source = str(media[0]['Part'][0].get('file', '')).replace('\\', '/')
        with self.lock:
            mappings = sorted(self.config['mappings'], key=lambda m: -len(m['plex']))
        for mapping in mappings:
            prefix = mapping['plex'] + '/'
            if not source.startswith(prefix):
                continue
            relative = PurePosixPath(source[len(prefix):])
            if '..' in relative.parts or relative.is_absolute():
                raise ValueError('Invalid Plex source path')
            base = Path(mapping['local']).resolve()
            target = (base / str(relative)).resolve()
            if base in target.parents and target.is_file() and any(r in target.parents for r in self.roots):
                return target
        raise ValueError('Plex original is not accessible; configure its path mapping in Media sources')

    def listing(self, kind, key, offset=0):
        endpoint = {'s': f'/library/sections/{key}/all', 'm': f'/library/metadata/{key}/children',
                    'p': f'/playlists/{key}/items'}[kind]
        return self.request(endpoint + '?' + urlencode({'X-Plex-Container-Start': offset,
                              'X-Plex-Container-Size': 100})).get('MediaContainer', {})

    def browse(self, root, path):
        self.require()
        result = dict(root=root, path=path, parent='', folders=[], videos=[])
        if path == ':plex:':
            sections = self.request('/library/sections').get('MediaContainer', {}).get('Directory', [])
            result['folders'] = [{'name': display_text(s['title']), 'path': ':plex:s' + str(s['key'])}
                                 for s in sections if s.get('type') in ('movie', 'show', 'artist')]
            result['folders'].append({'name': 'Playlists', 'path': ':plex:playlists'})
            return self.remember_parents(result)
        if path == ':plex:playlists':
            rows = self.request('/playlists?playlistType=audio,video').get('MediaContainer', {}).get('Metadata', [])
            result.update(parent=':plex:', folders=[{'name': display_text(r['title']),
                'path': ':plex:p' + str(r['ratingKey'])} for r in rows if r.get('playlistType') in ('audio', 'video')])
            return self.remember_parents(result)
        match = re.fullmatch(r':plex:([smp])(\d+)(?:@(\d+))?', path)
        if not match:
            raise ValueError('Invalid Plex folder')
        kind, key, offset = match.groups()
        offset = int(offset or 0)
        data = self.listing(kind, key, offset)
        rows = data.get('Metadata', [])
        result['parent'] = ':plex:playlists' if kind == 'p' else ':plex:'
        if kind == 'm' and data.get('parentRatingKey'):
            result['parent'] = ':plex:m' + str(data['parentRatingKey'])
        for index, row in enumerate(rows):
            title = display_text(row.get('title', 'Untitled'))
            rating = str(row['ratingKey'])
            if not rating.isdigit():
                continue
            if row.get('type') in ('movie', 'episode', 'track'):
                if row.get('type') == 'episode':
                    title = display_text(f"S{int(row.get('parentIndex', 0)):02}E{int(row.get('index', 0)):02} {title}")
                result['videos'].append({'id': self.token(f'{rating}.{kind}{key}.{offset + index}'),
                    'name': title, 'kind': 'audio' if row['type'] == 'track' else 'video', 'bytes': 0})
            else:
                result['folders'].append({'name': title, 'path': ':plex:m' + rating})
        if offset:
            result['folders'].append({'name': 'Previous page', 'path': f':plex:{kind}{key}@{max(0, offset - 100)}'})
        if offset + len(rows) < int(data.get('totalSize', offset + len(rows))):
            result['folders'].append({'name': 'Next page', 'path': f':plex:{kind}{key}@{offset + len(rows)}'})
        return self.remember_parents(result)

    def remember_parents(self, listing):
        with self.lock:
            base = listing['path'].split('@')[0]
            listing['parent'] = self.parents.get(base, listing['parent'])
            if len(self.parents) > 512:
                self.parents.clear()
            for folder in listing['folders']:
                if '@' not in folder['path']:
                    self.parents[folder['path']] = base
        return listing

    def next_media(self, token, shuffle=False, previous=False):
        rating, kind, key, index = self.split(token)
        if not kind:
            return {}
        index = int(index)
        current = self.listing(kind, key, index).get('Metadata', [])
        if not current or str(current[0].get('ratingKey')) != rating:
            return {}  # A changed playlist must not silently select the wrong item.
        wanted = index - 1 if previous else index + 1
        if shuffle and current[0].get('type') == 'track' and not previous:
            size = int(self.listing(kind, key).get('totalSize', 0))
            if size < 2:
                return {}
            wanted = random.randrange(size - 1)
            if wanted >= index:
                wanted += 1
        if wanted < 0:
            return {}
        rows = self.listing(kind, key, wanted).get('Metadata', [])
        if not rows or rows[0].get('type') not in ('movie', 'episode', 'track'):
            return {}
        row = rows[0]
        return {'id': self.token(f"{row['ratingKey']}.{kind}{key}.{wanted}"),
                'kind': 'audio' if row['type'] == 'track' else 'video'}

    def details(self, token):
        row = self.metadata(token)
        name = row.get('title', '')
        if row.get('type') == 'episode':
            name = f"{row.get('grandparentTitle', '')} S{int(row.get('parentIndex', 0)):02}E{int(row.get('index', 0)):02} {name}"
        elif row.get('type') == 'track' and row.get('grandparentTitle'):
            name = row['grandparentTitle'] + ' - ' + name
        return {'name': display_text(name, 126), 'title': display_text(row.get('title')), 'artist': display_text(row.get('grandparentTitle')),
                'album': display_text(row.get('parentTitle')), 'summary': display_text(row.get('summary'), 1200),
                'year': row.get('year'), 'resume': int(row.get('viewOffset', 0)) // 1000,
                'watched': bool(row.get('viewCount')), 'provider': 'plex'}

    def report(self, token, state, position, duration):
        key = self.split(token)[0]
        if state not in ('playing', 'paused', 'stopped'):
            raise ValueError('Invalid Plex playback state')
        position, duration = int(position), int(duration)
        if not 0 <= position <= 604800000 or not 0 <= duration <= 604800000:
            raise ValueError('Invalid Plex playback position')
        with self.report_condition:
            if self.closed:
                return
            last = self.last_report.get(key)
            now = time.monotonic()
            if last and last[0] == state and now - last[1] < 5:
                return
            if len(self.last_report) > 128:
                self.last_report.clear()
            self.last_report[key] = state, now
            if len(self.pending) >= 16 and key not in self.pending:
                self.pending.pop(next(iter(self.pending)))
            self.pending[key] = dict(_server=self.namespace(), ratingKey=key, key='/library/metadata/' + key,
                state=state, time=position, duration=duration,
                identifier='com.plexapp.plugins.library')
            if self.report_thread is None:
                self.report_thread = threading.Thread(target=self._reports, name='PlexTimeline', daemon=True)
                self.report_thread.start()
            self.report_condition.notify()

    def _reports(self):
        while True:
            with self.report_condition:
                while not self.pending and not self.closed:
                    self.report_condition.wait()
                if self.closed:
                    return
                key = next(iter(self.pending))
                report = self.pending.pop(key)
            try:
                with self.lock:
                    self.require()
                    if report.pop('_server') != self.namespace():
                        continue
                    self.request('/:/timeline?' + urlencode(report))
                self.report_error = ''
            except ValueError as exc:
                self.report_error = str(exc)

    def close(self):
        with self.report_condition:
            self.closed = True
            self.report_condition.notify_all()
        if self.report_thread:
            self.report_thread.join(timeout=9)

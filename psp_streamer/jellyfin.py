"""Jellyfin user-scoped catalogue and original-file adapter. No upstream transcoding."""
import hashlib
import json
import random
import re
import threading
import time
import uuid
from pathlib import Path, PurePosixPath
from urllib.parse import urlencode, urlsplit
from urllib.request import Request, build_opener
from urllib.error import URLError

from .plex import Plex, NoRedirect
from .plex_media import PlexMediaBridge
from .radio import display_text
from .timeline import chapters, jellyfin_markers
from .work_cache import WorkCache

ID = r'[0-9a-f]{32}'


def identifier(value):
    value = str(value or '').replace('-', '').lower()
    if not re.fullmatch(ID, value):
        raise ValueError('Invalid Jellyfin identifier')
    return value


def authorization(client, token=''):
    # Both values originate from persisted configuration/upstream JSON.
    if any(c in str(client) + str(token) for c in '\r\n"\\'):
        raise ValueError('Invalid Jellyfin credential')
    return (f'MediaBrowser Client="PSPStreamer", Device="PSP", DeviceId="{client}", '
            f'Version="1.0", Token="{token}"')


class JellyfinBridge(PlexMediaBridge):
    endpoint_pattern = rf'/Items/{ID}/Download'

    def headers(self, token, client):
        return {'Authorization': authorization(client, token)}


class Jellyfin(Plex):
    art_provider = 'jellyfin'
    def __init__(self, directory, roots):
        # Reuse bounded cache, parent navigation, atomic credential storage,
        # background report lifecycle and authenticated range bridge.
        super().__init__(directory, roots)
        self.path = Path(directory) / 'jellyfin.json'
        self.config = dict(client=str(uuid.uuid4()), enabled=False, token='',
                           url='', user='', username='', server='')
        if self.path.exists():
            self.config.update(json.loads(self.path.read_text(encoding='utf-8')))
        self.sessions = {}
        self.segment_cache = WorkCache(entries=128, jobs=4, timeout=4)

    def public(self):
        with self.lock:
            return {k: self.config[k] for k in ('enabled', 'url', 'username')} | {
                'selected': bool(self.config['token']), 'report_error': self.report_error}

    def request(self, path, *, method='GET', data=None, token=None, url=None, raw=False, timeout=8):
        with self.lock:
            base = url or self.config['url']
            headers = {'Authorization': authorization(self.config['client'],
                self.config['token'] if token is None else token), 'Accept': 'application/json',
                'Content-Type': 'application/json'}
        if not base:
            raise ValueError('Connect to Jellyfin first')
        try:
            with build_opener(NoRedirect()).open(Request(base + path,
                    data=None if data is None else json.dumps(data).encode(),
                    headers=headers, method=method), timeout=timeout) as response:
                limit = (16 if raw else 4)*1024*1024
                body = response.read(limit+1)
                if len(body) > limit:
                    raise ValueError('Jellyfin response is too large')
                return body if raw else json.loads(body) if body.strip() else {}
        except (URLError, OSError, ValueError):
            raise ValueError('Jellyfin request failed; check address, connection and credentials') from None

    def login(self, data):
        url = str(data.get('url', '')).strip().rstrip('/')
        parts = urlsplit(url)
        if parts.scheme not in ('https', 'http') or not parts.hostname or parts.username or parts.password or parts.query or parts.fragment:
            raise ValueError('Enter a Jellyfin HTTP(S) server address')
        username, password = data.get('username'), data.get('password')
        if not isinstance(username, str) or not username or not isinstance(password, str):
            raise ValueError('Enter Jellyfin username and password')
        result = self.request('/Users/AuthenticateByName', method='POST', url=url, token='',
                              data={'Username': username, 'Pw': password})
        user = identifier(result.get('User', {}).get('Id'))
        token = result.get('AccessToken')
        if not isinstance(token, str) or not token:
            raise ValueError('Jellyfin did not return an access token')
        authorization(self.config['client'], token)
        with self.lock:
            self.config.update(url=url, user=user, username=username, token=token,
                server=str(result.get('ServerId') or url), enabled=True)
            self.cache.clear(); self.parents.clear()
            with self.report_condition:
                self.pending.clear(); self.last_report.clear(); self.sessions.clear()
            self.save()
        return self.public()

    def configure(self, data):
        if not isinstance(data.get('enabled'), bool):
            raise ValueError('Jellyfin source switch must be a boolean')
        with self.lock:
            if data['enabled'] and not self.config['token']:
                raise ValueError('Connect to Jellyfin first')
            if not data['enabled'] and not getattr(self, 'additional_source', lambda: True)():
                raise ValueError('Keep at least one source enabled')
            self.config['enabled'] = data['enabled']
            self.save()
        return self.public()

    def disconnect(self):
        with self.lock:
            self.config.update(enabled=False, token='', user='', username='', url='', server='')
            self.cache.clear(); self.parents.clear()
            with self.report_condition:
                self.pending.clear(); self.last_report.clear(); self.sessions.clear()
            self.save()
        return self.public()

    def require(self):
        if not self.config['enabled'] or not self.config['token']:
            raise ValueError('Jellyfin source is disabled or disconnected')

    def namespace(self):
        return hashlib.sha256((self.config['server']+'|'+self.config['user']).encode()).hexdigest()[:12]

    def token(self, key, kind='', parent='', index=0):
        context = f'.{kind}{identifier(parent)}.{index}' if kind else ''
        return f'jellyfin.{identifier(key)}{context}.{self.namespace()}'

    def split(self, token):
        self.require()
        match = re.fullmatch(rf'jellyfin\.({ID})(?:\.([mp])({ID})\.([0-9]{{1,10}}))?\.([0-9a-f]{{12}})', token)
        if not match or match[5] != self.namespace():
            raise ValueError('Invalid Jellyfin media identifier')
        return match.groups()[:4]

    def metadata(self, token):
        key = self.split(token)[0]
        with self.lock:
            cached = self.cache.get(key)
            if cached and cached[0] > time.monotonic():
                return cached[1]
        row = self.request('/Users/'+self.config['user']+'/Items/'+key)
        with self.lock:
            if len(self.cache) >= 128:
                self.cache.clear()
            self.cache[key] = time.monotonic()+20, row
        return row

    def source(self, token):
        row = self.metadata(token)
        if row.get('Type') not in ('Movie', 'Episode', 'Audio', 'MusicVideo', 'Video'):
            raise ValueError('Jellyfin item is not playable media')
        if int(row.get('PartCount') or 1) > 1:
            raise ValueError('Multipart Jellyfin media is not supported')
        name = PurePosixPath(str(row.get('Path', '')).replace('\\', '/')).name
        if not PurePosixPath(name).suffix:
            extension = row.get('Container', '')
            if not re.fullmatch('[A-Za-z0-9]{1,8}', extension):
                raise ValueError('Jellyfin did not identify the original container')
            name = display_text(row.get('Name') or 'Jellyfin media')+'.'+extension
        with self.lock:
            if self.media_bridge is None:
                self.media_bridge = JellyfinBridge(self)
            return self.media_bridge.source({'key': '/Items/'+identifier(row['Id'])+'/Download',
                'size': row.get('Size', 0)}, name, row.get('Etag', row.get('DateLastMediaAdded', '')), identifier(row['Id']))

    def text_subtitle(self, token, track):
        row=self.metadata(token)
        sources=row.get('MediaSources') or []
        source=next((s for s in sources if s.get('Path')==row.get('Path')), {})
        if not source:
            return None  # Do not guess across alternative original versions.
        streams=sorted((s for s in source.get('MediaStreams', [])
                        if s.get('Type')=='Subtitle' and not s.get('IsExternal')),
                       key=lambda s:int(s['Index']))
        if not 0<=track<len(streams):
            return None
        stream=streams[track]
        if stream.get('Codec') not in ('ass','ssa','subrip','srt','webvtt','mov_text','text'):
            return None
        source_id=identifier(source['Id']); key=identifier(row['Id'])
        index=int(stream['Index'])
        if index<0:
            raise ValueError('Invalid Jellyfin subtitle stream index')
        data=self.request(f'/Videos/{key}/{source_id}/Subtitles/{index}/Stream.srt',raw=True,timeout=120)
        return data.decode('utf-8-sig',errors='replace')

    def listing(self, kind, key, offset=0):
        query = dict(UserId=self.config['user'], StartIndex=offset, Limit=100,
                     Fields='MediaSources,Overview,Genres', EnableUserData='true')
        if kind == 'p':
            path = '/Playlists/'+identifier(key)+'/Items'
        else:
            path = '/Users/'+self.config['user']+'/Items'
            query.update(ParentId=identifier(key), SortBy='SortName', SortOrder='Ascending')
        return self.request(path+'?'+urlencode(query))

    def browse(self, root, path):
        self.require()
        result = dict(root=root, path=path, parent=':jellyfin:', folders=[], videos=[])
        if path == ':jellyfin:':
            rows = self.request('/Users/'+self.config['user']+'/Views').get('Items', [])
            result.update(parent='', folders=[{'name': display_text(r['Name']),
                'path': ':jellyfin:m'+identifier(r['Id'])} for r in rows
                if r.get('CollectionType') in ('movies', 'tvshows', 'music', 'musicvideos', 'homevideos', 'boxsets', None)])
            result['folders'].append({'name': 'Playlists', 'path': ':jellyfin:playlists'})
            return self.remember_parents(result)
        playlists = re.fullmatch(r':jellyfin:playlists(?:@([0-9]{1,10}))?', path)
        if playlists:
            offset = int(playlists[1] or 0)
            data = self.request('/Users/'+self.config['user']+'/Items?'+urlencode({
                'Recursive': 'true', 'IncludeItemTypes': 'Playlist', 'Limit': 100,
                'StartIndex': offset, 'SortBy': 'SortName', 'SortOrder': 'Ascending'}))
            result['folders'] = [{'name': display_text(r['Name']), 'path': ':jellyfin:p'+identifier(r['Id'])}
                                 for r in data.get('Items', [])]
            count = len(result['folders'])
            if offset:
                result['folders'].append(dict(name='Previous page',path=f':jellyfin:playlists@{max(0,offset-100)}'))
            if count and offset+count<int(data.get('TotalRecordCount',0)):
                result['folders'].append(dict(name='Next page',path=f':jellyfin:playlists@{offset+count}'))
            return self.remember_parents(result)
        match = re.fullmatch(rf':jellyfin:([mp])({ID})(?:@([0-9]{{1,10}}))?', path)
        if not match:
            raise ValueError('Invalid Jellyfin folder')
        kind, key, offset = match.groups(); offset = int(offset or 0)
        data = self.listing(kind, key, offset); rows = data.get('Items', [])
        if kind == 'p':
            result['parent'] = ':jellyfin:playlists'
        for index, row in enumerate(rows):
            name = display_text(row.get('Name') or 'Untitled')
            if row.get('Type') in ('Movie', 'Episode', 'Audio', 'MusicVideo', 'Video'):
                if row['Type'] == 'Episode':
                    name = f"S{int(row.get('ParentIndexNumber') or 0):02}E{int(row.get('IndexNumber') or 0):02} {name}"
                result['videos'].append(dict(artwork=self.artwork.links(row, self.token(row['Id'])), id=self.token(row['Id'], kind, key, offset+index),
                    name=name, kind='audio' if row['Type']=='Audio' else 'video', bytes=0))
            elif row.get('IsFolder'):
                result['folders'].append(dict(name=name, path=':jellyfin:m'+identifier(row['Id']),
                    artwork=self.artwork.links(row, self.token(row['Id']))))
        for label, page in [('Previous page', max(0, offset-100)), ('Next page', offset+len(rows))]:
            if (label=='Previous page' and offset) or (label=='Next page' and rows and page<int(data.get('TotalRecordCount', page))):
                result['folders'].append(dict(name=label, path=f':jellyfin:{kind}{key}@{page}'))
        return self.remember_parents(result)

    def next_media(self, token, shuffle=False, previous=False):
        key, kind, parent, index = self.split(token)
        if not kind:
            return {}
        index = int(index); data = self.listing(kind, parent, index); rows = data.get('Items', [])
        if not rows or identifier(rows[0]['Id']) != key:
            return {}
        wanted = index-1 if previous else index+1
        if shuffle and rows[0].get('Type')=='Audio' and not previous:
            count = int(data.get('TotalRecordCount', 0))
            if count<2:
                return {}
            wanted = random.randrange(count-1)
            if wanted>=index: wanted+=1
        if wanted<0:
            return {}
        rows = self.listing(kind, parent, wanted).get('Items', [])
        if not rows or rows[0].get('Type') not in ('Movie', 'Episode', 'Audio', 'MusicVideo', 'Video'):
            return {}
        return dict(id=self.token(rows[0]['Id'], kind, parent, wanted),
                    kind='audio' if rows[0]['Type']=='Audio' else 'video')

    def details(self, token):
        row = self.metadata(token); user = row.get('UserData') or {}
        name = row.get('Name', '')
        if row.get('Type')=='Episode':
            name = f"{row.get('SeriesName', '')} S{int(row.get('ParentIndexNumber') or 0):02}E{int(row.get('IndexNumber') or 0):02} {name}"
        return dict(name=display_text(name,126), title=display_text(row.get('Name')),
            artist=display_text(', '.join(row.get('Artists') or [])), album=display_text(row.get('Album')),
            summary=display_text(row.get('Overview'),1200), year=row.get('ProductionYear'),
            resume=int(user.get('PlaybackPositionTicks') or 0)//10000000,
            watched=bool(user.get('Played')), provider='jellyfin',
            chapters=chapters(row.get('Chapters'), 'StartPositionTicks', 10000000, 'Name'),
            artwork=self.artwork.links(row, self.token(row['Id'])))

    def web_markers(self, token):
        key = self.split(token)[0]
        # Optional capability: old servers/absent providers must not block play.
        # Browser-only request, bounded and independent of compact PSP metadata.
        def load():
            try:
                data = self.request('/MediaSegments/' + key, timeout=2)
                return jellyfin_markers(data.get('Items'))
            except (ValueError, OSError, AttributeError):
                return []
        try:
            return self.segment_cache.get((self.namespace(), self.config['user'], key), load, ttl=30)
        except ValueError:
            return []

    def art_headers(self):
        return {'Authorization': authorization(self.config['client'], self.config['token']),
                'Accept': 'image/jpeg,image/png,image/webp'}

    def report(self, token, state, position, duration):
        key = self.split(token)[0]
        position, duration = int(position), int(duration)
        if state not in ('playing','paused','stopped') or not 0<=position<=604800000 or not 0<=duration<=604800000:
            raise ValueError('Invalid Jellyfin playback report')
        with self.report_condition:
            if self.closed: return
            last = self.last_report.get(key); now = time.monotonic()
            if last and last[0]==state and now-last[1]<5: return
            if len(self.last_report)>128: self.last_report.clear()
            self.last_report[key] = state, now
            if len(self.pending)>=16 and key not in self.pending: self.pending.pop(next(iter(self.pending)))
            self.pending[key] = (self.namespace(), token, state, position)
            if self.report_thread is None:
                self.report_thread=threading.Thread(target=self._reports,name='JellyfinTimeline',daemon=True)
                self.report_thread.start()
            self.report_condition.notify()

    def _reports(self):
        while True:
            with self.report_condition:
                while not self.pending and not self.closed: self.report_condition.wait()
                if self.closed: return
                key=next(iter(self.pending)); namespace,token,state,position=self.pending.pop(key)
            try:
                with self.lock:
                    self.require()
                    if namespace!=self.namespace(): continue
                if key not in self.sessions:
                    row=self.metadata(token)
                    sources=row.get('MediaSources', [])
                    source=next((s for s in sources if s.get('Path')==row.get('Path')), sources[0] if sources else {})
                    body=dict(ItemId=key, MediaSourceId=source.get('Id',key), PlaySessionId=uuid.uuid4().hex,
                        CanSeek=True, PlayMethod='DirectStream', PositionTicks=position*10000, IsPaused=state=='paused')
                    self.request('/Sessions/Playing',method='POST',data=body)
                    if len(self.sessions)>16: self.sessions.clear()
                    self.sessions[key]=body
                body=dict(self.sessions[key],PositionTicks=position*10000,IsPaused=state=='paused')
                self.request('/Sessions/Playing/'+('Stopped' if state=='stopped' else 'Progress'),method='POST',data=body)
                if state=='stopped': self.sessions.pop(key,None)
                self.report_error=''
            except (ValueError, KeyError, TypeError):
                self.report_error='Jellyfin playback reporting failed; check connection and permissions'

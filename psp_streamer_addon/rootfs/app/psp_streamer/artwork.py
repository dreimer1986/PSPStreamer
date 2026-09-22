"""Small authenticated image proxy. Artwork never enters the PSP media stream."""
from collections import OrderedDict
import hashlib
import re
import threading
import time
import struct
import subprocess
from urllib.parse import urlencode
from urllib.request import Request, build_opener
from urllib.error import URLError
from .work_cache import WorkCache


class Artwork:
    max_image = 8 * 1024 * 1024
    max_cache = 24 * 1024 * 1024

    def __init__(self, provider):
        self.provider = provider
        self.lock = threading.Lock()
        self.rows = OrderedDict()
        self.cache = OrderedDict()
        self.cache_bytes = 0
        self.slots = threading.BoundedSemaphore(4)
        self.psp_cache = WorkCache(entries=8, jobs=4)
        self.psp_planes = WorkCache(entries=16, jobs=4)
        self.conversions = threading.BoundedSemaphore(2)

    def _psp_scope(self, token):
        with self.provider.lock:
            self.provider.split(token)
            return (self.provider.art_provider, self.provider.config['url'], self.provider.config['token'],
                    self.provider.namespace())

    def psp_identity(self, token):
        """Canonical series/album artwork, scoped to source/account and image tags."""
        scope = self._psp_scope(token)
        row = self.provider.metadata(token)
        parent = (row.get('grandparentRatingKey') if row.get('type') == 'episode' else
                  row.get('parentRatingKey') if row.get('type') == 'track' else None) if self.provider.art_provider == 'plex' else (
                  row.get('SeriesId') if row.get('Type') in ('Episode','Season') else row.get('AlbumId') if row.get('Type') == 'Audio' else None)
        if parent:
            try:
                parent_token = self.provider.token(str(parent))
                row = self.provider.metadata(parent_token)
                token = parent_token
            except ValueError:
                pass
        self.links(row, token)
        paths = self.paths(row)
        if self._psp_scope(token) != scope:
            raise ValueError('Artwork source changed; retry')
        tag = hashlib.sha256(repr((scope, sorted(paths.items()))).encode()).hexdigest()
        return token, scope, paths, tag

    def _psp_plane(self, token, scope, kind, path, width, height):
        def convert():
            if not self.conversions.acquire(timeout=10):
                raise ValueError('Image conversion busy')
            try:
                image, _ = self.get(token, kind)
                mode = 'increase' if kind == 'backdrop' else 'decrease'
                fit = f'crop={width}:{height}' if kind == 'backdrop' else f'pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:black'
                result = subprocess.run(['ffmpeg','-v','error','-max_alloc','16777216','-threads','1',
                    '-i','pipe:0','-an','-vf',f'scale={width}:{height}:force_original_aspect_ratio={mode},{fit}',
                    '-threads','1','-frames:v','1','-pix_fmt','rgb565le','-f','rawvideo','pipe:1'],
                    input=image, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=5)
                if result.returncode or len(result.stdout) != width*height*2:
                    raise ValueError('Image conversion failed')
                if self._psp_scope(token) != scope:
                    raise ValueError('Artwork source changed; retry')
                return result.stdout
            finally:
                self.conversions.release()
        return self.psp_planes.get((scope, kind, path, width, height), convert, ttl=600)

    def psp(self, token, known=None):
        """v1 raw packets; v2 identifies/reuses the client's one retained image."""
        token, scope, paths, tag = self.psp_identity(token)
        if known == tag:
            return b'PSPK'+tag.encode('ascii')
        def prepare():
            planes = []
            for kind, width, height in [('backdrop',320,180), ('cover',80,112)]:
                data = b''
                if paths.get(kind):
                    try:
                        data = self._psp_plane(token,scope,kind,paths[kind],width,height)
                    except (ValueError, OSError, subprocess.TimeoutExpired):
                        pass
                planes.append(data)
            background, cover = planes
            return struct.pack('<4sHHHHII',b'PSPA',320,180,80,112,len(background),len(cover))+background+cover
        # Incomplete packets retry rather than retaining a transient failure.
        packet = self.psp_cache.get((scope,tag),prepare,ttl=600,
            cache_if=lambda p: all(not paths.get(k) or struct.unpack_from('<I',p,offset)[0]
                                   for k,offset in [('backdrop',12),('cover',16)]))
        if self._psp_scope(token) != scope:
            raise ValueError('Artwork source changed; retry')
        if known is None:
            return packet
        complete = all(not paths.get(k) or struct.unpack_from('<I',packet,offset)[0]
                       for k,offset in [('backdrop',12),('cover',16)])
        # Do not give partial failures a reusable identity.
        return b'PSPI'+(tag if complete else '0'*64).encode('ascii')+packet

    def paths(self, row):
        if self.provider.art_provider == 'plex':
            def path(*fields):
                for field in fields:
                    value = row.get(field, '')
                    # Never forward credentials to an absolute URL or an arbitrary
                    # Plex endpoint. Provider-local metadata images only.
                    if isinstance(value, str) and re.fullmatch(r'/library/metadata/[0-9]+/(?:thumb|art)(?:/[0-9]+)?', value):
                        return value
                return ''
            return {'cover': path('thumb', 'parentThumb', 'grandparentThumb'),
                    'backdrop': path('art', 'grandparentArt', 'parentArt')}

        def image(key, kind, tag):
            if not re.fullmatch(r'[0-9a-fA-F]{32}', str(key or '').replace('-', '')) or not tag:
                return ''
            return '/Items/'+str(key).replace('-', '')+'/Images/'+kind+'?'+urlencode({
                'tag': tag, 'maxWidth': 1280 if kind == 'Backdrop' else 480, 'quality': 85, 'format': 'Jpg'})
        tags = row.get('ImageTags') or {}
        own = image(row.get('Id'), 'Primary', tags.get('Primary')) or image(row.get('Id'), 'Thumb', tags.get('Thumb'))
        cover = own or image(row.get('AlbumId'), 'Primary', row.get('AlbumPrimaryImageTag')) or image(
            row.get('SeriesId'), 'Primary', row.get('SeriesPrimaryImageTag')) or image(
            row.get('ParentThumbItemId'), 'Thumb', row.get('ParentThumbImageTag'))
        backs = row.get('BackdropImageTags') or []
        parent = row.get('ParentBackdropImageTags') or []
        backdrop = image(row.get('Id'), 'Backdrop', backs[0] if backs else None) or image(
            row.get('ParentBackdropItemId'), 'Backdrop', parent[0] if parent else None)
        return dict(cover=cover, backdrop=backdrop)

    def links(self, row, token):
        paths = {k: v for k, v in self.paths(row).items() if v}
        with self.lock:
            self.rows[token] = (time.monotonic() + 600, paths)
            self.rows.move_to_end(token)
            while len(self.rows) > 1024:
                self.rows.popitem(last=False)
        return {kind: '/api/artwork/'+token+'/'+kind+'?v='+hashlib.sha256(path.encode()).hexdigest()[:12]
                for kind, path in paths.items()}

    def get(self, token, kind):
        if kind not in ('cover', 'backdrop'):
            raise ValueError('Unknown artwork kind')
        self.provider.split(token)  # Includes enabled/source-namespace validation, even on cache hits.
        with self.lock:
            hint = self.rows.get(token)
        if not hint or hint[0] < time.monotonic():
            self.links(self.provider.metadata(token), token)
            with self.lock:
                hint = self.rows.get(token)
        path = hint[1].get(kind) if hint else None
        if not path:
            raise ValueError('Artwork unavailable')
        with self.provider.lock:
            base = self.provider.config['url']
            headers = self.provider.art_headers()
        # Account changes must not serve another user's cached images.
        key = hashlib.sha256(repr((base, headers, path, kind)).encode()).digest()
        with self.lock:
            cached = self.cache.get(key)
            if cached and cached[0] > time.monotonic():
                self.cache.move_to_end(key)
                return cached[1], cached[2]
        if not self.slots.acquire(timeout=5):
            raise ValueError('Artwork busy')
        try:
            if self.provider.art_provider == 'plex':
                path = '/photo/:/transcode?' + urlencode(dict(url=path, width=1280 if kind=='backdrop' else 480,
                    height=720, minSize=0, upscale=0))
            from .plex import NoRedirect
            with build_opener(NoRedirect()).open(Request(base+path, headers=headers), timeout=5) as response:
                mime = response.headers.get_content_type()
                if mime not in ('image/jpeg', 'image/png', 'image/webp'):
                    raise ValueError('Unsupported artwork format')
                data = response.read(self.max_image+1)
                if not data or len(data) > self.max_image:
                    raise ValueError('Artwork too large')
        except (URLError, OSError) as err:
            raise ValueError('Artwork unavailable') from err
        finally:
            self.slots.release()
        with self.lock:
            previous = self.cache.pop(key, None)
            if previous:
                self.cache_bytes -= len(previous[1])
            while self.cache and (self.cache_bytes+len(data) > self.max_cache or len(self.cache) >= 128):
                self.cache_bytes -= len(self.cache.popitem(last=False)[1][1])
            self.cache[key] = (time.monotonic()+600, data, mime)
            self.cache_bytes += len(data)
        return data, mime

    def theme(self, token):
        """User-requested theme song preview, never a PSP playback command."""
        key = self.provider.split(token)[0]
        if self.provider.art_provider == 'plex':
            row = self.provider.metadata(token)
            path = next((row.get(field) for field in ('theme','parentTheme','grandparentTheme')
                         if re.fullmatch(r'/library/metadata/[0-9]+/theme(?:/[0-9]+)?', str(row.get(field, '')))), '')
            if not path and str(row.get('grandparentRatingKey', '')).isdigit():
                parent = self.provider.metadata(self.provider.token(str(row['grandparentRatingKey'])))
                candidate = parent.get('theme', '')
                if isinstance(candidate, str) and re.fullmatch(r'/library/metadata/[0-9]+/theme(?:/[0-9]+)?', candidate):
                    path = candidate
        else:
            songs = self.provider.request('/Items/'+key+'/ThemeSongs?'+urlencode({
                'userId': self.provider.config['user'], 'inheritFromParent': 'true'})).get('Items', [])
            item = str(songs[0].get('Id', '')).replace('-', '') if songs else ''
            path = '/Audio/'+item+'/stream?static=true' if re.fullmatch(r'[0-9a-fA-F]{32}', item) else ''
        if not path:
            raise ValueError('No theme song available')
        with self.provider.lock:
            base, headers = self.provider.config['url'], self.provider.art_headers()
        headers['Accept'] = 'audio/*'
        if not self.slots.acquire(timeout=2):
            raise ValueError('Artwork busy')
        try:
            from .plex import NoRedirect
            with build_opener(NoRedirect()).open(Request(base+path, headers=headers), timeout=10) as response:
                mime = response.headers.get_content_type()
                if mime not in ('audio/mpeg','audio/mp3','audio/mp4','audio/x-m4a','audio/flac','audio/ogg','audio/wav','audio/x-wav'):
                    raise ValueError('Unsupported theme format')
                data = response.read(16*1024*1024+1)
                if not data or len(data)>16*1024*1024:
                    raise ValueError('Theme too large')
                return data, mime
        except (URLError, OSError) as err:
            raise ValueError('Theme unavailable') from err
        finally:
            self.slots.release()

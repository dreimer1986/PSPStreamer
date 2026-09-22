"""Small authenticated image proxy. Artwork never enters the PSP media stream."""
from collections import OrderedDict
import hashlib
import re
import threading
import time
from urllib.parse import urlencode
from urllib.request import Request, build_opener
from urllib.error import URLError


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

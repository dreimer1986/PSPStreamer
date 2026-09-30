"""Read-only account Watchlist, mapped by GUID to the selected Plex server.

Discover IDs are NOT local ratingKeys and never reach the playback API.
Endpoint/mapping reference: python-plexapi MyPlexAccount.watchlist.
"""
import re
from concurrent.futures import ThreadPoolExecutor
from urllib.parse import urlencode
from .radio import display_text

PAGE = 8  # Two batches of four bounded local queries, not eight serial waits.


def browse(provider, offset=0):
    provider.require()
    with provider.lock:
        enabled = provider.config.get('watchlist', False)
        account = provider.config.get('account', '')
        identity = (provider.config.get('url'), provider.config.get('token'), provider.namespace())
    if not enabled or not account:
        raise ValueError('Enable Plex Watchlist in Settings first')
    if type(offset) is not int or not 0 <= offset <= 100000:
        raise ValueError('Invalid Watchlist page')
    query = urlencode({'X-Plex-Container-Start': offset, 'X-Plex-Container-Size': PAGE,
                       'includeCollections': 1, 'includeExternalMedia': 1})
    data = provider.request('/library/sections/watchlist/all?' + query,
                            url='https://discover.provider.plex.tv', token=account, timeout=5).get('MediaContainer', {})
    rows = data.get('Metadata', [])[:PAGE]
    result = dict(folders=[], videos=[], unavailable=[], offset=offset, page_size=PAGE,
                  next=offset + PAGE if offset + len(rows) < int(data.get('totalSize', offset + len(rows))) else None)
    def match(row):
        kind, guid = row.get('type'), row.get('guid', '')
        title = display_text(row.get('title'))
        if kind not in ('movie', 'show') or not isinstance(guid, str) or not guid.startswith('plex://') or len(guid) > 256:
            return row, []
        query = urlencode({'guid': guid, 'type': 1 if kind == 'movie' else 2,
                           'includeGuids': 1, 'X-Plex-Container-Size': 100})
        local = provider.request('/library/all?' + query, timeout=3).get('MediaContainer', {}).get('Metadata', [])
        matches = [item for item in local if item.get('type') == kind and
                   (item.get('guid') == guid or any(g.get('id') == guid for g in item.get('Guid', []))) and
                   re.fullmatch(r'[0-9]{1,20}', str(item.get('ratingKey', '')))]
        return row, matches
    # No persistent polling/thread pool. All workers finish before returning;
    # failures are errors, never falsely classified as unavailable titles.
    with ThreadPoolExecutor(max_workers=4, thread_name_prefix='watchlist-map') as pool:
        mapped = list(pool.map(match, rows))
    with provider.lock:
        if account != provider.config.get('account') or identity != (provider.config.get('url'), provider.config.get('token'), provider.namespace()):
            raise ValueError('Plex connection changed; refresh the Watchlist')
    seen = set()
    for row, matches in mapped:
        kind, title = row.get('type'), display_text(row.get('title'))
        if not matches:
            result['unavailable'].append(dict(name=title, reason='Not available on the selected Plex server'
                                              if str(row.get('guid', '')).startswith('plex://') else 'No stable Plex GUID'))
        for item in matches:
            key = str(item['ratingKey'])
            if key in seen:
                continue
            seen.add(key)
            token = provider.token(key)
            title = display_text(item.get('title'))
            art = provider.artwork.links(item, token)
            if kind == 'show':
                result['folders'].append(dict(name=title, path=':plex:m' + key, artwork=art))
            else:
                from .media_versions import version_folder
                folder = version_folder(provider, item, token, title)
                if folder:
                    result['folders'].append(folder)
                else:
                    result['videos'].append(dict(id=token, name=title, kind='video', artwork=art))
    return result

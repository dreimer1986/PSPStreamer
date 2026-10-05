"""Xbox adapters for shared, durable library conveniences. No PSP commands."""
from .catalogue import browse


def listing(server, root, path, offset, query=''):
    if offset < 0:
        raise ValueError('Invalid page')
    queue = server.playlist.snapshot()
    extra = dict(revision=queue['revision'], enabled=int(queue['enabled']),
                 repeat=queue['repeat'], shuffle=int(queue['shuffle']))
    records = server.comfort.snapshot()['records']
    favorites = {r['id'] for r in records if r.get('favorite')}
    if path == ':xbox:queue:':
        queue = server.playlist.snapshot()
        rows = [dict(r, kind=r.get('kind', 'video'), position=i) for i, r in enumerate(queue['items'])]
        extra.update(revision=queue['revision'], enabled=int(queue['enabled']),
                     repeat=queue['repeat'], shuffle=int(queue['shuffle']))
        parent = ''
    elif path == ':xbox:search:':
        result = server.search.snapshot(query)
        rows = [dict(name=r['name'], id=r['id'], path=r['id'], root=r['root'],
                     kind='folder' if r['folder'] else 'audio' if r['audio'] else 'video')
                for r in result['results']]
        extra.update(running=int(result['running']), limited=int(result['limited']),
                     errors=len(result['errors']))
        parent = ''
    elif path in (':xbox:favorites:', ':xbox:recent:'):
        rows = [dict(name=r['name'], id=r['id'], path=r['id'],
                     kind='folder' if r.get('folder') else 'audio' if r.get('audio') else 'video')
                for r in records if r.get('favorite' if path == ':xbox:favorites:' else 'used')]
        parent = ''
    else:
        data = browse(server, root, path)
        root, path, parent = data['root'], data['path'], data['parent']
        rows = [dict(r, kind='folder') for r in data['folders']] + data['videos']
        if not path:
            rows = [dict(name=n, path=p, kind='folder') for n, p in (
                ('Favorites', ':xbox:favorites:'), ('Recently played', ':xbox:recent:'),
                ('Playlist', ':xbox:queue:'), ('Search', ':xbox:search:'))] + rows
    entries = []
    for row in rows[offset:offset+64]:
        item = {k: row[k] for k in ('name', 'id', 'path', 'root', 'kind', 'position') if k in row}
        item['favorite'] = int(row.get('id', row.get('path')) in favorites)
        entries.append(item)
    return dict(root=root, path=path, parent=parent, total=len(rows), offset=offset, entries=entries, **extra)


def change(server, request):
    if not isinstance(request, dict):
        raise ValueError('Invalid Xbox library action')
    if 'id_hex' in request:
        request = dict(request, id=bytes.fromhex(request['id_hex']).decode('utf-8'))
    action = request.get('action')
    if action == 'favorite':
        token = request.get('id')
        row = server.radio.get(token) if isinstance(token,str) and token.startswith('radio.') else server.playlist_item({'id': token})
        name = bytes.fromhex(request.get('name_hex','')).decode('utf-8') or row.get('name') or token
        return server.comfort.change(dict(action='favorite', id=token, name=name[:128],
                                         audio=token.startswith('radio.') or row.get('kind') == 'audio', value=request.get('value')))
    if action == 'add':
        name = bytes.fromhex(request.get('name_hex','')).decode('utf-8')
        request = dict(request, items=[{'id': request.get('id'), 'name':name[:200] or None}])
    if action not in ('add', 'remove', 'move', 'enabled', 'repeat', 'shuffle'):
        raise ValueError('Unsupported Xbox library action')
    return server.playlist.change(request, server.playlist_item)


def next_media(server, token, previous=False, manual=False, shuffle=False, repeat_one=False):
    """Queue rules win over folder preferences; manual skips bypass repeat-one."""
    queued = server.playlist.next(token, previous, manual)
    if queued is not None:
        return queued
    if token.startswith('radio.'):
        server.radio.get(token)
        return {}
    if repeat_one and not manual and not previous:
        server.library.decode(token)
        return {'id':token}
    return server.library.next_media(token, shuffle, previous)

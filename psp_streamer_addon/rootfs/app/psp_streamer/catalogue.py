"""Source-level navigation shared by the web UI and the existing PSP protocol."""
import re


def browse(server, root=0, path=''):
    sources = server.plex.public()
    if path in ('', '.'):
        folders = []
        if sources['files']:
            folders.append({'name': 'Files', 'path': ':files:'})
        if sources['enabled']:
            folders.append({'name': 'Plex', 'path': ':plex:'})
        if sources['radio']:
            folders.append({'name': 'Internet Radio', 'path': ':radio:'})
        return dict(root=0, path='', parent=None, folders=folders, videos=[])
    if path.startswith(':plex:'):
        return server.plex.browse(root, path)
    if path == ':radio:' and sources['radio']:
        return server.radio.browse(root)
    if not sources['files']:
        raise ValueError('Filesystem source is disabled')
    if path == ':files:':
        return dict(root=0, path=path, parent='', videos=[], folders=[
            {'name': folder.name or str(folder), 'path': f':files:/{index}'}
            for index, folder in enumerate(server.library.roots)])
    # Slash between the source and root also preserves the PSP's existing
    # lexical parent navigation, without requiring a client update.
    match = re.fullmatch(r':files:/?(\d+)(?:/(.*))?', path)
    if match:
        root, relative = int(match[1]), match[2] or ''
        listing = server.library.browse(root, relative)
        prefix = f':files:/{root}'
        listing['path'] = prefix + ('/'+relative if relative not in ('', '.') else '')
        listing['parent'] = ':files:' if listing['parent'] is None else prefix + (
            '/'+listing['parent'] if listing['parent'] not in ('', '.') else '')
        for folder in listing['folders']:
            folder['path'] = prefix+'/'+folder['path']
        return listing
    if path.startswith(':'):
        raise ValueError('Unknown media source')
    # Legacy paths remain usable, including old browser bookmarks.
    listing = server.library.browse(root, path)
    if listing['parent'] == '.':
        listing['parent'] = ''
    return listing


def folder_media(server, root, path, recursive=False):
    """Bound traversal, deduplicate IDs, and follow Plex pagination explicitly."""
    if path in ('', '.', ':files:', ':plex:', ':radio:'):
        raise ValueError('Select a media folder, not a source overview')
    # "This folder" includes earlier Plex pages, even if the user opened it
    # from page two. Pagination is not a child folder.
    if path.startswith(':plex:'):
        path = path.split('@')[0]
    pending, seen, media = [path], set(), {}
    while pending:
        current = pending.pop(0)
        if current in seen:
            continue
        seen.add(current)
        if len(seen) > 256:
            raise ValueError('Too many folders; select a smaller folder')
        listing = browse(server, root, current)
        for item in listing['videos']:
            if not item.get('live') and not item['id'].startswith('radio.'):
                media.setdefault(item['id'], item)
            if len(media) > 128:
                raise ValueError('At most 128 files per batch; select a smaller folder')
        for folder in listing['folders']:
            child = folder['path']
            if current.startswith(':plex:') and '@' in child:
                if child.split('@')[0] == current.split('@')[0] and int(child.split('@')[1]) > int(current.split('@')[1] if '@' in current else 0):
                    pending.append(child)
            elif recursive:
                pending.append(child)
    return list(media.values())

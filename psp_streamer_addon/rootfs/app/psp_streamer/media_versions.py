"""Stable original-version IDs, shared by browser and unmodified PSP clients."""
import hashlib
import re
from .radio import display_text


def split_version(token):
    parts = token.split('~')
    if len(parts) == 1:
        return token, None
    if len(parts) != 2 or not re.fullmatch('[0-9a-f]{16}', parts[1]):
        raise ValueError('Invalid media version')
    return parts


def version_id(source, jellyfin=False):
    key = source.get('Id') if jellyfin else source.get('id')
    if key is None:
        key = source.get('Path') if jellyfin else [p.get('key') for p in source.get('Part', [])]
    return hashlib.sha256(str(key).encode()).hexdigest()[:16]


def selected(provider, token):
    _, wanted = split_version(token)
    row = provider.metadata(token)
    jf = provider.art_provider == 'jellyfin'
    sources = row.get('MediaSources' if jf else 'Media') or []
    if wanted:
        source = next((s for s in sources if version_id(s, jf) == wanted), None)
        if source is None:
            raise ValueError('Selected media version is no longer available; refresh the library')
        return source
    if jf:
        return next((s for s in sources if s.get('Path') == row.get('Path')), sources[0] if sources else {})
    if not sources:
        raise ValueError('No original media version available')
    return sources[0]


def version_folder(provider, row, token, name):
    sources = row.get('MediaSources' if provider.art_provider == 'jellyfin' else 'Media') or []
    if len(sources) < 2:
        return None
    return {'name': name, 'path': ':versions:'+token,
            'artwork': provider.artwork.links(row, split_version(token)[0])}


def browse_versions(provider, token, root):
    base, _ = split_version(token)
    row = provider.metadata(base)
    jf = provider.art_provider == 'jellyfin'
    sources = row.get('MediaSources' if jf else 'Media') or []
    result = dict(root=root, path=':versions:'+base,
        parent=provider.parents.get(':versions:'+base, ':jellyfin:' if jf else ':plex:'), folders=[], videos=[])
    for index, source in enumerate(sources):
        if jf:
            label = source.get('Name') or source.get('Path', '').replace('\\','/').rsplit('/',1)[-1]
            kind = 'audio' if row.get('Type') == 'Audio' else 'video'
        else:
            parts = source.get('Part') or [{}]
            label = parts[0].get('file','').replace('\\','/').rsplit('/',1)[-1]
            if not label:
                label = ' '.join(str(source.get(k) or '') for k in ('videoResolution','videoCodec','audioCodec'))
            kind = 'audio' if row.get('type') == 'track' else 'video'
        result['videos'].append(dict(id=base+'~'+version_id(source,jf),
            name=display_text(f'{index+1}: {label or "Original"}',126), kind=kind, bytes=0,
            artwork=provider.artwork.links(row,base)))
    return result

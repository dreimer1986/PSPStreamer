"""Subtitle files for browser/Xbox burn-in, isolated from PSP overlay delivery."""
from .work_cache import WorkCache
from .media_versions import selected
from .jellyfin import identifier

_cache = WorkCache(entries=8, jobs=2, timeout=130)


def seek_timeline(command, seconds):
    """libass reads original subtitle times; input -ss rebases the video to zero.

    Restore source time only while filtering, then return to the stream clock.
    Kept out of the PSP command builder and bitmap overlay path.
    """
    if seconds and '-vf' in command:
        at = command.index('-vf') + 1
        if 'subtitles=' in command[at]:
            command[at] = f'setpts=PTS+{seconds:.3f}/TB,' + command[at] + f',setpts=PTS-{seconds:.3f}/TB'
    return command


def jellyfin_burnin(provider, token, track):
    """Fetch embedded text via Jellyfin, not a second full original-file scan.

    Keep ASS styling where available. Bitmap/unsupported tracks return None
    and retain their existing video-filter route. No PSP behavior is changed.
    """
    source = selected(provider, token)
    if not source:
        return None
    streams = sorted((s for s in source.get('MediaStreams', [])
                      if s.get('Type') == 'Subtitle' and not s.get('IsExternal')),
                     key=lambda s: int(s['Index']))
    if not 0 <= track < len(streams):
        return None
    stream = streams[track]
    codec = str(stream.get('Codec', '')).lower()
    if codec not in ('ass', 'ssa', 'subrip', 'srt', 'webvtt', 'mov_text', 'text'):
        return None
    extension = 'ass' if codec in ('ass', 'ssa') else 'srt'
    key = identifier(provider.split(token)[0])
    source_id = identifier(source['Id'])
    index = int(stream['Index'])
    if index < 0:
        raise ValueError('Invalid Jellyfin subtitle index')
    namespace = tuple(provider.config.get(k) for k in ('url', 'user', 'token'))

    def fetch():
        body = provider.request(f'/Videos/{key}/{source_id}/Subtitles/{index}/Stream.{extension}',
                                raw=True, timeout=120)
        if not body or len(body) > 4*1024*1024:
            raise ValueError('Empty or oversized text subtitle')
        return extension, body

    return _cache.get((namespace, token, source_id, index, extension), fetch, ttl=60)

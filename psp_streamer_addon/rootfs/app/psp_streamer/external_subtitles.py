"""Sidecar tracks belonging to the selected original, never arbitrary client URLs."""
import re
import subprocess
from .media_versions import selected
from .work_cache import WorkCache

_cache = WorkCache(entries=8,jobs=2,timeout=130)
TEXT = {'srt':'subrip','subrip':'subrip','ass':'ass','ssa':'ssa','webvtt':'webvtt','vtt':'webvtt','text':'subrip'}


def provider_for(library,token):
    return library.plex if token.startswith('plex.') else library.jellyfin if token.startswith('jellyfin.') else None


def tracks(library,token):
    provider=provider_for(library,token)
    if provider is None:return []
    source=selected(provider,token)
    jf=provider.art_provider=='jellyfin'
    streams=source.get('MediaStreams',[]) if jf else (source.get('Part') or [{}])[0].get('Stream',[])
    external=[]
    for stream in streams:
        if jf:
            if stream.get('Type')!='Subtitle' or not stream.get('IsExternal'):continue
            codec=str(stream.get('Codec','')).lower()
            language=stream.get('Language','und');title=stream.get('DisplayTitle') or stream.get('Title','')
            forced=stream.get('IsForced',False);sdh=stream.get('IsHearingImpaired',False)
        else:
            if str(stream.get('streamType'))!='3' or not stream.get('key'):continue
            codec=str(stream.get('codec','')).lower()
            language=stream.get('languageCode','und');title=stream.get('displayTitle') or stream.get('title','')
            forced=stream.get('forced',False);sdh=stream.get('hearingImpaired',False)
        # Preserve unsupported rows as explicit unsupported tracks; never
        # silently map their ordinal to a different embedded subtitle.
        normalized=TEXT.get(codec,'hdmv_pgs_subtitle' if codec in ('pgs','pgssub','sup','hdmv_pgs_subtitle') else codec)
        external.append({'codec_type':'subtitle','codec_name':normalized,
            'tags':{'language':language,'title':'External | '+str(title)},
            'disposition':{'forced':int(bool(forced)),'hearing_impaired':int(bool(sdh))},
            '_provider_stream':stream})
    return external


def payload(library,token,track,source=None,probe=None):
    """Return (codec, canonical bytes) for a sidecar ordinal, or None if embedded."""
    if track<0:return None
    external=tracks(library,token)
    if not external:return None
    if probe is None:
        from .probe_cache import probe as inspect
        import json
        source=source if source is not None else library.decode(token)[1]
        result=inspect(source,['-show_entries','stream=codec_type','-of','json'])
        if result.returncode:raise ValueError('Could not resolve external subtitle index')
        probe=json.loads(result.stdout)
    count=sum(s.get('codec_type')=='subtitle' for s in probe.get('streams',[]) if not s.get('_provider_stream'))
    if track<count:return None
    if track-count>=len(external):raise ValueError('Selected external subtitle is unavailable')
    entry=external[track-count];stream=entry['_provider_stream'];codec=entry['codec_name']
    provider=provider_for(library,token)
    def load():
        if provider.art_provider=='jellyfin':
            from .jellyfin import identifier
            chosen=selected(provider,token);key=provider.split(token)[0]
            index=int(stream['Index'])
            if index<0:raise ValueError('Invalid external subtitle index')
            if codec not in set(TEXT.values())|{'hdmv_pgs_subtitle'}:
                raise ValueError('Unsupported external subtitle format (text or single-file PGS required)')
            # Jellyfin calls raw PGS "pgssub" even when its file ends in .sup.
            # Requesting .sup would attempt an unsupported text conversion.
            extension='pgssub' if codec=='hdmv_pgs_subtitle' else 'srt'
            body=provider.request(f'/Videos/{key}/{identifier(chosen["Id"])}/Subtitles/{index}/Stream.{extension}',raw=True,timeout=120)
            return codec if extension=='pgssub' else 'subrip',body
        key=stream.get('key','')
        if not re.fullmatch(r'/library/streams/[0-9]+(?:\.[A-Za-z0-9]+)?',key):
            raise ValueError('Invalid Plex external subtitle endpoint')
        if codec not in set(TEXT.values())|{'hdmv_pgs_subtitle'}:
            raise ValueError('Unsupported external subtitle format (text or single-file PGS required)')
        body=provider.request(key,raw=True)
        if codec=='hdmv_pgs_subtitle':return codec,body
        if codec=='subrip':return 'subrip',body
        result=subprocess.run(['ffmpeg','-v','error','-protocol_whitelist','pipe','-f',codec,
            '-i','pipe:0','-f','srt','pipe:1'],input=body,capture_output=True,timeout=30)
        if result.returncode:raise ValueError('Could not convert external subtitle to SRT')
        return 'subrip',result.stdout
    # Include account and version; a reconnection must not reuse another user's data.
    namespace=(provider.config.get('url'),provider.config.get('user'),provider.config.get('token'))
    return _cache.get((namespace,token,str(stream.get('Index',stream.get('id'))),codec),load,ttl=60)

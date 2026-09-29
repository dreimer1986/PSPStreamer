"""Durable language/title preferences, never cross-episode stream indices."""
import hashlib
import json
import os
import tempfile
import threading
from pathlib import Path


class SeriesPreferences:
    def __init__(self, directory):
        self.path=Path(directory)/'series-preferences.json'
        self.lock=threading.RLock()
        self.data=json.loads(self.path.read_text()) if self.path.exists() else {}

    def context(self, server, token):
        for name in ('plex','jellyfin'):
            if token.startswith(name+'.'):
                provider=getattr(server,name);row=provider.metadata(token)
                key=row.get('grandparentRatingKey') if name=='plex' else row.get('SeriesId')
                if not key:return ''
                # Account/server changes must not inherit another library's settings.
                namespace=json.dumps({k:provider.config.get(k,'') for k in ('url','server','user','account')},sort_keys=True)
                return name+':'+hashlib.sha256(namespace.encode()).hexdigest()+':'+str(key)
        if token.startswith(('radio.','dlna.')):return ''
        _,source=server.library.decode(token)
        # File sources do not have a series database: scope explicitly to folder.
        return 'folder:'+hashlib.sha256(str(source.parent.resolve()).encode()).hexdigest()

    @staticmethod
    def match(rows, preference, fallback):
        if preference is None:return -1
        def language(code):
            code=code.casefold()
            return {'ger':'de','deu':'de','eng':'en','jpn':'ja','fra':'fr','fre':'fr','spa':'es','ita':'it'}.get(code,code)
        candidates=[r for r in rows if language(r.get('l','und'))==language(preference['language'])]
        exact=next((r for r in candidates if r.get('t','')==preference['title']),None)
        return int((exact or candidates[0])['n']) if candidates else fallback

    def attach(self, server, token, payload):
        result=dict(payload)
        key=self.context(server,token) if payload.get('kind')!='audio' else ''
        result['series_scope']='folder' if key.startswith('folder:') else 'series' if key else ''
        with self.lock:policy=self.data.get(key)
        result['series_saved']=int(policy is not None)
        if policy:
            result['preferred_audio']=self.match(payload['a'],policy['audio'],0)
            result['preferred_subtitle']=self.match(payload['s'],policy['subtitle'],-1)
        return result

    def save(self, server, token, payload, request):
        key=self.context(server,token)
        if not key or payload.get('kind')=='audio':raise ValueError('No series or folder preference for this media')
        def selected(field,rows):
            number=request.get(field)
            if type(number) is not int:raise ValueError('Invalid track preference')
            if field=='subtitle' and number==-1:return None
            row=next((r for r in rows if int(r['n'])==number),None)
            if row is None:raise ValueError('Track is unavailable')
            return dict(language=row.get('l','und'),title=row.get('t',''))
        remove=request.get('remove') is True
        policy=None if remove else dict(audio=selected('audio',payload['a']),subtitle=selected('subtitle',payload['s']))
        with self.lock:
            draft=dict(self.data)
            if remove:draft.pop(key,None)
            else:draft[key]=policy
            fd,temp=tempfile.mkstemp(prefix='.series-',dir=self.path.parent)
            try:
                with os.fdopen(fd,'w') as output:
                    json.dump(draft,output,ensure_ascii=False);output.flush();os.fsync(output.fileno())
                os.replace(temp,self.path)
            finally:
                if os.path.exists(temp):os.unlink(temp)
            self.data=draft
        return self.attach(server,token,payload)

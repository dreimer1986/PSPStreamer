"""Durable, shared favorites/history; never stores PSP connection profiles."""
import json
import os
from pathlib import Path
import re
import tempfile
import threading
import time


class Comfort:
    def __init__(self, directory):
        self.path = Path(directory) / 'comfort.json'
        self.lock = threading.RLock()
        self.last_id = ''
        self.last_save = 0
        self.data = {'sequence': 0, 'records': {}, 'clients': {}}
        if self.path.exists():
            self.data = json.loads(self.path.read_text())

    def save(self):
        fd, name = tempfile.mkstemp(prefix='.comfort-', dir=self.path.parent)
        try:
            with os.fdopen(fd, 'w') as f:
                json.dump(self.data, f, ensure_ascii=False)
                f.flush(); os.fsync(f.fileno())
            os.replace(name, self.path)
        finally:
            if os.path.exists(name): os.unlink(name)

    def record(self, token, name='', folder=0, audio=0):
        if not isinstance(token, str) or not token or len(token.encode()) > 511 or any(ord(c)<32 for c in token):
            raise ValueError('Invalid shortcut')
        if not isinstance(name, str) or len(name.encode()) > 512:
            raise ValueError('Invalid shortcut name')
        records = self.data['records']
        if token not in records:
            if len(records) >= 512:
                candidates = [r for r in records.values() if not r['favorite']]
                if not candidates: raise ValueError('Favorites storage is full')
                del records[min(candidates, key=lambda r:r['used'])['id']]
            records[token] = dict(id=token, name=name or token, folder=int(bool(folder)), audio=int(bool(audio)),
                                  favorite=0, seconds=0, used=0)
        r = records[token]
        if name: r.update(name=name, folder=int(bool(folder)), audio=int(bool(audio)))
        return r

    def used(self, r):
        self.data['sequence'] += 1
        r['used'] = self.data['sequence']

    def snapshot(self):
        with self.lock:
            return {'records': [dict(r) for r in sorted(self.data['records'].values(),key=lambda r:r['used'],reverse=True)]}

    def change(self, data):
        if not isinstance(data, dict): raise ValueError('Invalid shortcut')
        action = data.get('action')
        if action not in ('favorite', 'forget', 'restart'): raise ValueError('Invalid shortcut action')
        with self.lock:
            r = self.record(data.get('id'), data.get('name',''), data.get('folder',0), data.get('audio',0))
            if action == 'favorite':
                if type(data.get('value')) is not bool: raise ValueError('Invalid favorite value')
                r['favorite'] = int(data['value'])
            elif action == 'forget': r['used'] = r['seconds'] = 0
            else: r['seconds'] = 0
            self.save()
            return {'ok': True}

    def report(self, query, metadata):
        """Reuse existing telemetry, with disk writes at most once per 30 s."""
        token = (query.get('media') or query.get('plex') or query.get('radio') or [''])[0]
        state = query.get('state',[''])[0]
        if not token or state not in ('playing','paused','stopped') or query.get('started')==['0']: return
        try:
            position = max(0,min(86400,int(query.get('position',['0'])[0])//1000))
            duration = max(0,int(query.get('duration',['0'])[0])//1000)
            with self.lock:
                r=self.record(token,str(metadata.get('title',''))[:128],0,metadata.get('kind')=='audio')
                changed=token!=self.last_id
                if changed: self.used(r)
                self.last_id = '' if state=='stopped' else token
                r['seconds']=0 if r['audio'] or token.startswith('radio.') or (duration and position>=duration-3) else position
                if changed or state=='stopped' or time.monotonic()-self.last_save>=30:
                    self.save(); self.last_save=time.monotonic()
        except (ValueError, TypeError, OSError):
            pass  # A history write must never break the media remote.

    @staticmethod
    def wire(r):
        # The PSP has a 128-byte title field. Do not split a UTF-8 character.
        name=r['name'].encode()[:127].decode('utf-8',errors='ignore')
        return [r['id'].encode().hex(),name.encode().hex(),r['folder'],r['audio'],r['favorite'],r['seconds'],r['used']]

    def sync(self, data):
        if not isinstance(data,dict) or not re.fullmatch(r'[0-9a-f]{16}',str(data.get('client',''))):
            raise ValueError('Invalid comfort client')
        rows=data.get('records')
        if not isinstance(rows,list) or len(rows)>64: raise ValueError('Too many shortcuts')
        validated=[]
        for row in rows:
            if not isinstance(row,list) or len(row)!=7: raise ValueError('Invalid shortcut row')
            if any(not isinstance(v,str) for v in row[:2]): raise ValueError('Invalid shortcut text')
            token,name=(bytes.fromhex(v).decode('utf-8') for v in row[:2])
            if not token or len(token.encode())>511 or len(name.encode())>127 or any(ord(c)<32 for c in token+name):
                raise ValueError('Invalid shortcut text')
            if any(type(v) is not int for v in row[2:]) or any(v not in (0,1) for v in row[2:5]) or not 0<=row[5]<=86400 or not 0<=row[6]<=0xffffffff:
                raise ValueError('Invalid shortcut values')
            validated.append((token,name,row))
        with self.lock:
            clients=self.data['clients']; client=data['client']; previous=clients.get(client,{})
            # Compare with the last returned snapshot. An
            # unchanged PSP must not resurrect a favorite removed in the web UI.
            for token,name,row in sorted(validated,key=lambda v:v[2][6]):
                r=self.record(token,name,row[2],row[3]); old=previous.get(token)
                if old:
                    for index,field in [(4,'favorite'),(5,'seconds')]:
                        if row[index] != old[1][index]: r[field]=row[index]
                    if row[6] != old[1][6]:
                        if row[6]: self.used(r)
                        else: r['used']=0
                else:
                    r['favorite'] |= row[4]
                    if not r['used'] and row[6]: self.used(r); r['seconds']=row[5]
            output=sorted(self.data['records'].values(),key=lambda r:(r['favorite'],r['used']),reverse=True)[:64]
            # Explicit zero fields for forgotten entries are retained: they
            # are tombstones, not candidates for re-import from an old PSP.
            sent={r['id']:self.wire(r) for r in output}
            received={token:row for token,_,row in validated}
            clients[client]={token:[received.get(token,wire),wire] for token,wire in sent.items()}
            while len(clients)>16: del clients[next(iter(clients))]
            self.save()
            return {'records':[list(row) for row in sent.values()]}

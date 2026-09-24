"""UPnP ContentDirectory source. SSDP runs only on explicit discovery requests."""
import base64
from concurrent.futures import ThreadPoolExecutor
import hashlib
import hmac
import json
import mimetypes
import os
from pathlib import Path
import random
import re
import socket
import tempfile
import threading
import time
from urllib.parse import urljoin, urlsplit
from urllib.request import Request, build_opener
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape

from .plex import NoRedirect
from .plex_media import PlexMediaBridge, RemoteSource
from .radio import display_text
from .work_cache import WorkCache

SERVICE = 'urn:schemas-upnp-org:service:ContentDirectory:'


def http_url(value, parent=None):
    if not isinstance(value, str) or len(value)>2048 or any(ord(c)<32 for c in value):
        raise ValueError('Invalid DLNA HTTP address')
    url = urljoin(parent, value) if parent else value
    parts = urlsplit(url)
    if parts.scheme not in ('http','https') or not parts.hostname or parts.username or parts.password or parts.fragment:
        raise ValueError('DLNA requires an HTTP(S) address without credentials')
    if parent and parts.hostname.lower() != urlsplit(parent).hostname.lower():
        raise ValueError('DLNA resource points to another host')
    return url


def xml(data):
    if b'<!DOCTYPE' in data.upper() or b'<!ENTITY' in data.upper():
        raise ValueError('DLNA XML declarations are not permitted')
    try:
        return ET.fromstring(data)
    except ET.ParseError:
        raise ValueError('Invalid DLNA XML response') from None


def fetch(url, data=None, headers=None):
    try:
        with build_opener(NoRedirect()).open(Request(http_url(url), data=data, headers=headers or {}), timeout=5) as response:
            body=response.read(4*1024*1024+1)
            if len(body)>4*1024*1024:
                raise ValueError('DLNA response exceeds size limit')
            return body
    except OSError:
        raise ValueError('DLNA server is unavailable') from None


def text(node, name, default=''):
    return node.findtext('{*}'+name, default)


def encode(value):
    return base64.urlsafe_b64encode(value.encode()).decode().rstrip('=') or '-'


def decode(value):
    if value=='-': return ''
    if not re.fullmatch('[A-Za-z0-9_-]{1,300}',value):
        raise ValueError('Invalid DLNA object identifier')
    try:
        return base64.urlsafe_b64decode(value+'='*(-len(value)%4)).decode('utf-8')
    except (ValueError, UnicodeError):
        raise ValueError('Invalid DLNA object identifier') from None


class DlnaBridge(PlexMediaBridge):
    def headers(self, token, client):
        return {'transferMode.dlna.org': 'Streaming'}

    def resource(self, device, url, name):
        payload=base64.urlsafe_b64encode(json.dumps([device,url],separators=(',',':')).encode()).decode().rstrip('=')
        signature=hmac.new(self.secret,payload.encode(),hashlib.sha256).hexdigest()
        return RemoteSource(f'http://127.0.0.1:{self.server_port}/{payload}.{signature}',name,
                            hashlib.sha256(payload.encode()).hexdigest())

    def resolve(self, path):
        if len(path)>8192 or not re.fullmatch(r'/[A-Za-z0-9_-]+\.[a-f0-9]{64}',path):
            raise ValueError('Invalid DLNA capability')
        payload,signature=path[1:].split('.')
        if not hmac.compare_digest(signature,hmac.new(self.secret,payload.encode(),hashlib.sha256).hexdigest()):
            raise ValueError('Invalid DLNA capability')
        device,url=json.loads(base64.urlsafe_b64decode(payload+'='*(-len(payload)%4)))
        config=self.plex.device(device)
        return http_url(url,config['url']),'',''


class Dlna:
    def __init__(self,directory):
        self.path=Path(directory)/'dlna.json'
        self.lock=threading.RLock()
        self.config={'enabled':False,'devices':{}}
        if self.path.exists(): self.config.update(json.loads(self.path.read_text()))
        self.cache=WorkCache(entries=32,jobs=4,timeout=8)
        self.bridge=None

    def public(self):
        with self.lock:
            return {'enabled':self.config['enabled'], 'devices':[
                {'id':key,'name':value['name'],'url':value['url']} for key,value in self.config['devices'].items()]}

    def save(self):
        self.path.parent.mkdir(parents=True,exist_ok=True)
        fd,name=tempfile.mkstemp(prefix='.dlna-',dir=self.path.parent)
        try:
            with os.fdopen(fd,'w') as out:
                json.dump(self.config,out);out.flush();os.fsync(out.fileno())
            os.replace(name,self.path)
        finally:
            if os.path.exists(name):os.unlink(name)

    def add(self,url):
        url=http_url(url);document=xml(fetch(url))
        base=http_url(text(document,'URLBase') or url,url)
        service=next((s for s in document.findall('.//{*}service') if text(s,'serviceType').startswith(SERVICE)),None)
        if service is None: raise ValueError('No DLNA ContentDirectory service at this description URL')
        kind=text(service,'serviceType')
        if not re.fullmatch(re.escape(SERVICE)+r'[1-9][0-9]*',kind):raise ValueError('Invalid ContentDirectory service')
        if not text(service,'controlURL'):raise ValueError('Missing DLNA control URL')
        control=http_url(text(service,'controlURL'),base)
        node=document.find('.//{*}device')
        identity=text(node,'UDN') if node is not None else url
        key=hashlib.sha256((identity or url).encode()).hexdigest()[:16]
        with self.lock:
            if len(self.config['devices'])>=32 and key not in self.config['devices']:
                raise ValueError('At most 32 DLNA servers')
            self.config['devices'][key]={'url':url,'control':control,'service':kind,
                'name':display_text(text(node,'friendlyName') if node is not None else 'DLNA server')}
            self.config['enabled']=True;self.save()
        return self.public()

    def configure(self,data):
        with self.lock:
            if 'remove' in data:
                self.config['devices'].pop(str(data['remove']),None)
            if 'enabled' in data:
                if not isinstance(data['enabled'],bool):raise ValueError('DLNA source switch must be boolean')
                if not data['enabled'] and not getattr(self,'additional_source',lambda:True)():
                    raise ValueError('Keep at least one source enabled')
                self.config['enabled']=data['enabled']
            self.save()
        return self.public()

    def discover(self):
        urls=set()
        request=('M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\n'
                 'MAN: "ssdp:discover"\r\nMX: 2\r\nST: urn:schemas-upnp-org:device:MediaServer:1\r\n\r\n').encode()
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM,socket.IPPROTO_UDP) as sock:
            sock.setsockopt(socket.IPPROTO_IP,socket.IP_MULTICAST_TTL,2)
            sock.sendto(request,('239.255.255.250',1900))
            end=time.monotonic()+2.5
            while time.monotonic()<end and len(urls)<8:
                sock.settimeout(max(.01,end-time.monotonic()))
                try: body,_=sock.recvfrom(8192)
                except socket.timeout: break
                headers={line.split(':',1)[0].strip().lower():line.split(':',1)[1].strip()
                         for line in body.decode('latin-1').split('\r\n')[1:] if ':' in line}
                if headers.get('location'):urls.add(headers['location'])
        def add(url):
            try:self.add(url);return True
            except (ValueError,OSError):return False
        with ThreadPoolExecutor(max_workers=4) as pool: results=list(pool.map(add,sorted(urls)))
        return dict(self.public(), discovered=sum(results))

    def device(self,key):
        with self.lock:
            if not self.config['enabled'] or key not in self.config['devices']:
                raise ValueError('DLNA source disabled or server removed')
            return dict(self.config['devices'][key])

    def rows(self,device,object_id,offset=0,metadata=False):
        config=self.device(device)
        def load():
            action='BrowseMetadata' if metadata else 'BrowseDirectChildren'
            body=(f'<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">'
                  f'<s:Body><u:Browse xmlns:u="{config["service"]}"><ObjectID>{escape(object_id)}</ObjectID>'
                  f'<BrowseFlag>{action}</BrowseFlag><Filter>*</Filter><StartingIndex>{offset}</StartingIndex>'
                  '<RequestedCount>100</RequestedCount><SortCriteria></SortCriteria></u:Browse></s:Body></s:Envelope>')
            response=xml(fetch(config['control'],body.encode(),{'Content-Type':'text/xml; charset="utf-8"',
                'SOAPAction':f'"{config["service"]}#Browse"'}))
            result=response.find('.//{*}Result')
            if result is None:raise ValueError('DLNA Browse failed')
            document=xml((result.text or '').encode())
            if len(document)>100:raise ValueError('DLNA server ignored the requested page size')
            rows=[]
            for node in document:
                oid=node.get('id',''); parent=node.get('parentID','0')
                lengths=(len(encode(oid)),len(encode(parent)))
                if max(lengths)>300 or sum(lengths)>380:
                    raise ValueError('DLNA object identifiers exceed the PSP protocol limit')
                resources=[]
                for resource in node.findall('{*}res'):
                    protocol=resource.get('protocolInfo','').split(':',3)
                    if len(protocol)!=4 or protocol[0]!='http-get':continue
                    mime=protocol[2].lower()
                    if not mime.startswith(('audio/','video/')) and mime not in ('application/ogg','application/octet-stream'):continue
                    try:url=http_url(resource.text or '',config['url'])
                    except ValueError:continue
                    resources.append(dict(url=url,mime=mime,converted='DLNA.ORG_CI=1' in protocol[3],size=resource.get('size','0')))
                resources.sort(key=lambda r:r['converted'])
                rows.append(dict(id=oid,parent=parent,name=display_text(text(node,'title') or 'Untitled',126),
                    folder=node.tag.endswith('}container') or node.tag=='container',resources=resources))
            try:total=int(response.findtext('.//{*}TotalMatches',str(len(rows))))
            except ValueError:total=len(rows)
            return rows,total
        return self.cache.get((device,config['control'],object_id,offset,metadata),load,ttl=5)

    def token(self,device,row):
        return f'dlna.{device}.{encode(row["id"])}.{encode(row["parent"])}'

    def split(self,token):
        match=re.fullmatch(r'dlna\.([a-f0-9]{16})\.([A-Za-z0-9_-]+)\.([A-Za-z0-9_-]+)',token)
        if not match or len(token)>480:raise ValueError('Invalid DLNA media identifier')
        self.device(match[1]);return match[1],decode(match[2]),decode(match[3])

    def item(self,token):
        device,oid,_=self.split(token)
        rows,_=self.rows(device,oid,metadata=True)
        row=next((r for r in rows if r['id']==oid and not r['folder']),None)
        if not row or not row['resources']:raise ValueError('No HTTP audio/video resource offered by this DLNA server')
        return device,row

    def entry(self,device,row):
        return dict(id=self.token(device,row),name=row['name'],bytes=0,
            kind='audio' if row['resources'][0]['mime'].startswith('audio/') else 'video')

    def browse(self,root,path):
        result=dict(root=root,path=path,parent=':dlna:',folders=[],videos=[])
        if path==':dlna:':
            result['parent']=''
            if self.config['enabled']:
                result['folders']=[dict(name=d['name'],path=f':dlna:{key}:{encode("0")}') for key,d in self.config['devices'].items()]
            return result
        match=re.fullmatch(r':dlna:([a-f0-9]{16}):([A-Za-z0-9_-]+)(?:@([0-9]{1,9}))?',path)
        if not match:raise ValueError('Invalid DLNA folder')
        device,oid,offset=match[1],decode(match[2]),int(match[3] or 0)
        rows,total=self.rows(device,oid,offset)
        if oid!='0':
            meta,_=self.rows(device,oid,metadata=True)
            if meta:result['parent']=f':dlna:{device}:{encode(meta[0]["parent"])}'
        for row in rows:
            if row['folder']:result['folders'].append(dict(name=row['name'],path=f':dlna:{device}:{encode(row["id"])}'))
            elif row['resources']:result['videos'].append(self.entry(device,row))
        if offset:result['folders'].append(dict(name='Previous page',path=path.split('@')[0]+f'@{max(0,offset-100)}'))
        if rows and offset+len(rows)<total:result['folders'].append(dict(name='Next page',path=path.split('@')[0]+f'@{offset+len(rows)}'))
        return result

    def source(self,token):
        device,row=self.item(token);resource=row['resources'][0]
        from .server import MEDIA_EXTENSIONS
        suffix=Path(urlsplit(resource['url']).path).suffix.lower()
        if suffix not in MEDIA_EXTENSIONS:
            suffix={'audio/mpeg':'.mp3','audio/flac':'.flac','audio/x-flac':'.flac','video/x-matroska':'.mkv',
                    'video/mp2t':'.ts','audio/mp4':'.m4a'}.get(resource['mime'],mimetypes.guess_extension(resource['mime']) or '')
        if suffix not in MEDIA_EXTENSIONS:raise ValueError('Unsupported DLNA media format')
        name=row['name'] if Path(row['name']).suffix.lower()==suffix else row['name']+suffix
        with self.lock:
            if self.bridge is None:self.bridge=DlnaBridge(self)
            with self.bridge.pause_lock:
                if len(self.bridge.pause_sources)>128:self.bridge.pause_sources.clear()
                self.bridge.pause_sources.setdefault(resource['url'],(token,False,0))
            return self.bridge.resource(device,resource['url'],name)

    def next_media(self,token,shuffle=False,previous=False):
        device,oid,parent=self.split(token);entries=[]
        for offset in range(0,1000,100):
            rows,total=self.rows(device,parent,offset)
            entries.extend(r for r in rows if not r['folder'] and r['resources'])
            if not rows or offset+len(rows)>=total:break
        else:raise ValueError('DLNA folder too large for automatic next-file selection')
        current=next((i for i,r in enumerate(entries) if r['id']==oid),None)
        if current is None:return {}
        audio=entries[current]['resources'][0]['mime'].startswith('audio/')
        entries=[r for r in entries if r['resources'][0]['mime'].startswith('audio/')==audio]
        current=next(i for i,r in enumerate(entries) if r['id']==oid)
        index=current-1 if previous else current+1
        if shuffle and audio and not previous:
            choices=[i for i in range(len(entries)) if i!=current]
            if not choices:return {}
            index=random.choice(choices)
        return self.entry(device,entries[index]) if 0<=index<len(entries) else {}

    def close(self):
        if self.bridge:self.bridge.close()

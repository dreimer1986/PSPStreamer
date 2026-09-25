"""Bounded background catalogue search using the same adapters as browsing."""
from collections import deque
import threading
import time
import unicodedata


def folded(value):
    return unicodedata.normalize('NFKC', value).casefold()


class Search:
    def __init__(self, server):
        self.server=server
        self.lock=threading.Lock()
        self.query=''
        self.results=[]
        self.errors=[]
        self.pending=0
        self.visited=0
        self.limited=False
        self.started=0

    def snapshot(self, query):
        query=query.strip()
        if not 2<=len(query)<=120: raise ValueError('Enter 2–120 characters')
        with self.lock:
            if query!=self.query or (not self.pending and time.monotonic()-self.started>120):
                if self.pending: raise ValueError('A search is still running; please wait')
                from .catalogue import browse
                sources=browse(self.server)['folders']
                self.query=query;self.results=[];self.errors=[];self.visited=0;self.limited=False;self.started=time.monotonic()
                self.pending=len(sources)
                for source in sources:
                    threading.Thread(target=self.worker,args=(source,query),daemon=True).start()
            return dict(query=self.query,results=list(self.results),errors=list(self.errors),
                        running=bool(self.pending),visited=self.visited,limited=self.limited)

    def worker(self, source, query):
        from .catalogue import browse
        queue=deque([(0,source['path'])]);seen=set();deadline=time.monotonic()+60
        needle=folded(query);count=0
        try:
            while queue:
                if time.monotonic()>deadline or count>=2000:
                    with self.lock:self.limited=True
                    break
                root,path=queue.popleft()
                if (root,path) in seen:continue
                seen.add((root,path));count+=1
                try:data=browse(self.server,root,path)
                except (ValueError,OSError):
                    with self.lock:
                        message=source['name']+': some folders could not be read'
                        if message not in self.errors:self.errors.append(message)
                    continue
                found=[]
                for folder in data['folders']:
                    if len(queue)<4000:queue.append((data['root'],folder['path']))
                    else:
                        with self.lock:self.limited=True
                    if folder['name'] not in ('Next page','Previous page') and needle in folded(folder['name']):
                        found.append(dict(id=folder['path'],name=folder['name'],folder=1,audio=0,root=data['root'],source=source['name']))
                for item in data['videos']:
                    if needle in folded(item['name']):
                        found.append(dict(id=item['id'],name=item['name'],folder=0,audio=int(item.get('kind')=='audio'),root=data['root'],source=source['name']))
                with self.lock:
                    self.visited+=1
                    known={r['id'] for r in self.results}
                    for row in found:
                        if row['id'] not in known:
                            if len(self.results)>=300:self.limited=True;return
                            self.results.append(row);known.add(row['id'])
                time.sleep(.005)  # Do not monopolize the server between local folders.
        except Exception:
            with self.lock:self.errors.append(source['name']+': source search failed')
        finally:
            with self.lock:self.pending-=1

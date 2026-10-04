"""Bounded one-level season continuation, never a recursive library walk."""
import re


def season_number(name):
    match=re.fullmatch(r'(?:season|staffel|s)[ ._-]*(\d{1,3})(?:[ ._-]+.*)?', str(name).strip(), re.I)
    return int(match[1]) if match else None


def provider_next(provider, token, plex, previous=False):
    row=provider.metadata(token)
    if row.get('type' if plex else 'Type') != ('episode' if plex else 'Episode'):
        return {}
    series=row.get('grandparentRatingKey' if plex else 'SeriesId')
    current=row.get('parentIndex' if plex else 'ParentIndexNumber')
    if not series or current is None:return {}
    def rows(key):
        out=[]
        for offset in range(0,1000,100):
            data=provider.listing('m',str(key),offset)
            page=data.get('Metadata' if plex else 'Items',[])
            out.extend(page)
            if not page or offset+len(page)>=int(data.get('totalSize' if plex else 'TotalRecordCount',offset+len(page))):return out
        raise ValueError('Season listing too large for automatic continuation')
    seasons=[]
    for season in rows(series):
        if season.get('type' if plex else 'Type')!=('season' if plex else 'Season'):continue
        number=season.get('index' if plex else 'IndexNumber')
        if number is None:continue
        number=int(number)
        if (number<int(current)) if previous else (number>int(current)):seasons.append((number,season))
    for _,season in sorted(seasons,key=lambda x:x[0],reverse=previous)[:20]:
        key=str(season['ratingKey' if plex else 'Id'])
        episodes=[(i,r) for i,r in enumerate(rows(key)) if r.get('type' if plex else 'Type')==('episode' if plex else 'Episode')]
        if not episodes:continue
        i,episode=episodes[-1 if previous else 0]
        rating=str(episode['ratingKey' if plex else 'Id'])
        token=provider.token(f'{rating}.m{key}.{i}') if plex else provider.token(rating,'m',key,i)
        return dict(id=token,kind='video',name=str(episode.get('title' if plex else 'Name','')))
    return {}


def local_next(directory, root, video_extensions, sort_key, previous=False):
    current=season_number(directory.name)
    if current is None or directory==root:return None
    candidates=[]
    for sibling in directory.parent.iterdir():
        number=season_number(sibling.name)
        if number is None or not sibling.is_dir() or root not in sibling.resolve().parents:continue
        if (number<current) if previous else (number>current):candidates.append((number,sibling))
    for _,folder in sorted(candidates,key=lambda x:(x[0],x[1].name),reverse=previous)[:20]:
        files=sorted((p for p in folder.iterdir() if not p.name.startswith('.') and p.is_file()
            and p.suffix.lower() in video_extensions and root in p.resolve().parents),key=lambda p:(sort_key(p.name),p.name))
        if files:return files[-1 if previous else 0]
    return None


def dlna_next(provider,device,parent,previous=False):
    meta,_=provider.rows(device,parent,metadata=True)
    season=next((r for r in meta if r['id']==parent),None)
    if not season:return {}
    current=season_number(season['name'])
    if current is None:return {}
    def all_rows(key):
        output=[]
        for offset in range(0,1000,100):
            rows,total=provider.rows(device,key,offset);output.extend(rows)
            if not rows or offset+len(rows)>=total:return output
        raise ValueError('DLNA season listing too large')
    candidates=[]
    for row in all_rows(season['parent']):
        number=season_number(row['name'])
        if not row['folder'] or number is None:continue
        if (number<current) if previous else (number>current):candidates.append((number,row))
    for _,row in sorted(candidates,key=lambda x:x[0],reverse=previous)[:20]:
        files=[r for r in all_rows(row['id']) if not r['folder'] and r['resources'] and r['resources'][0]['mime'].startswith('video/')]
        if files:return provider.entry(device,files[-1 if previous else 0])
    return {}

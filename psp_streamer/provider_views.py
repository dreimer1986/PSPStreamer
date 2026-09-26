"""Fresh user-scoped provider shelves; web-only, bounded and read-only."""
import re
from urllib.parse import urlencode
from .radio import display_text
from .jellyfin import identifier

PAGE=50


def sections(provider, name):
    provider.require()
    if name=='plex':
        rows=provider.request('/library/sections').get('MediaContainer',{}).get('Directory',[])
        return [dict(id=str(r['key']),name=display_text(r.get('title')),kind=r['type']) for r in rows
                if r.get('type') in ('movie','show','artist') and str(r.get('key','')).isdigit()]
    rows=provider.request('/Users/'+provider.config['user']+'/Views').get('Items',[])
    return [dict(id=identifier(r['Id']),name=display_text(r.get('Name')),kind=r.get('CollectionType','')) for r in rows]


def browse(provider, name, view, section='', offset=0):
    provider.require()
    if view not in ('continue','recent','unwatched','collections'):raise ValueError('Invalid provider view')
    if not 0<=offset<=100000:raise ValueError('Invalid provider page')
    if name=='plex':
        query={'X-Plex-Container-Start':offset,'X-Plex-Container-Size':PAGE}
        if view=='continue':endpoint='/hubs/continueWatching/items'
        else:
            libraries=sections(provider,name)
            library=next((row for row in libraries if row['id']==section),None)
            if not library:raise ValueError('Select a provider library')
            endpoint='/library/sections/'+section+'/'+('collections' if view=='collections' else 'all')
            if view!='collections':
                query['type']={'movie':1,'show':4,'artist':10}[library['kind']]
                query['sort']='addedAt:desc' if view=='recent' else 'titleSort:asc'
                if view=='unwatched':query['unwatched']=1
        data=provider.request(endpoint+'?'+urlencode(query)).get('MediaContainer',{})
        rows=data.get('Metadata',[])
        more=offset+len(rows)<int(data.get('totalSize',offset+len(rows)))
    else:
        query=dict(UserId=provider.config['user'],StartIndex=offset,Limit=PAGE,Recursive='true',
                   Fields='MediaSources,Overview',EnableUserData='true')
        if section:query['ParentId']=identifier(section)
        if view=='continue':
            query['MediaTypes']='Video'
            data=provider.request('/Users/'+provider.config['user']+'/Items/Resume?'+urlencode(query))
            nextup=provider.request('/Shows/NextUp?'+urlencode(query))
            rows=data.get('Items',[])+nextup.get('Items',[])
            more=any(offset+len(d.get('Items',[]))<int(d.get('TotalRecordCount',0)) for d in (data,nextup))
        else:
            query['IncludeItemTypes']='BoxSet' if view=='collections' else 'Movie,Episode,Audio,MusicVideo,Video'
            query['SortBy']='DateCreated' if view=='recent' else 'SortName'
            query['SortOrder']='Descending' if view=='recent' else 'Ascending'
            if view=='unwatched':query['IsPlayed']='false'
            data=provider.request('/Users/'+provider.config['user']+'/Items?'+urlencode(query))
            rows=data.get('Items',[])
            more=offset+len(rows)<int(data.get('TotalRecordCount',0))
    result=dict(folders=[],videos=[],offset=offset,next=offset+PAGE if more else None)
    seen=set()
    for row in rows:
        plex=name=='plex';key=str(row.get('ratingKey','')) if plex else identifier(row.get('Id'))
        if not key or key in seen or (plex and not re.fullmatch(r'[0-9]{1,20}',key)):continue
        seen.add(key)
        kind=row.get('type') if plex else row.get('Type')
        token=provider.token(key)
        title=display_text(row.get('title') if plex else row.get('Name'))
        if kind in ('episode','Episode'):
            series=row.get('grandparentTitle','') if plex else row.get('SeriesName','')
            season=row.get('parentIndex',0) if plex else row.get('ParentIndexNumber',0)
            episode=row.get('index',0) if plex else row.get('IndexNumber',0)
            title=display_text(f'{series} S{int(season or 0):02}E{int(episode or 0):02} {title}')
        art=provider.artwork.links(row,token)
        if kind in ('movie','episode','track','Movie','Episode','Audio','MusicVideo','Video'):
            user=row.get('UserData') or {}
            seconds=int(row.get('viewOffset',0) or 0)//1000 if plex else int(user.get('PlaybackPositionTicks',0) or 0)//10000000
            with provider.lock:provider.cache.pop(key,None)  # Do not reuse another client's old resume point.
            from .media_versions import version_folder
            folder=version_folder(provider,row,token,title)
            if folder:result['folders'].append(folder);continue
            result['videos'].append(dict(id=token,name=title,artwork=art,kind='audio' if kind in ('track','Audio') else 'video',resume=seconds))
        else:
            path=(':plex:'+('c' if kind=='collection' else 'm')+key) if plex else ':jellyfin:m'+key
            result['folders'].append(dict(name=title,path=path,artwork=art))
    return result


def natural_context(provider, token, plex):
    """A shelf entry resumes its own season/album, not the changing shelf order."""
    row=provider.metadata(token)
    if (row.get('type') if plex else row.get('Type')) not in ('episode','track','Episode','Audio'):return None
    parent=row.get('parentRatingKey') if plex else (row.get('SeasonId') or row.get('ParentId'))
    if not parent:return None
    parent=str(parent) if plex else identifier(parent)
    if plex and not re.fullmatch(r'[0-9]{1,20}',parent):return None
    key=provider.split(token)[0]
    for offset in range(0,2000,100):
        data=provider.listing('m',parent,offset)
        rows=data.get('Metadata' if plex else 'Items',[])
        for index,item in enumerate(rows):
            itemkey=str(item.get('ratingKey')) if plex else identifier(item.get('Id'))
            if itemkey==key:return 'm',parent,offset+index
        if len(rows)<100:break
    return None

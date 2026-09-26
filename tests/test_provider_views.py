import tempfile
import unittest
from unittest.mock import patch
from urllib.parse import urlparse,parse_qs
from psp_streamer.plex import Plex
from psp_streamer.jellyfin import Jellyfin
from psp_streamer.provider_views import browse

A,B,P='a'*32,'b'*32,'c'*32


class ProviderViewsTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.plex=Plex(self.temp.name,[]);self.plex.config.update(enabled=True,server='test')
        self.jf=Jellyfin(self.temp.name,[]);self.jf.config.update(enabled=True,token='test',user=A,server='test')
        self.addCleanup(self.plex.close);self.addCleanup(self.jf.close)

    def test_plex_user_resume_collections_and_server_filters(self):
        calls=[]
        def request(path):
            calls.append(path);url=urlparse(path);q=parse_qs(url.query)
            if url.path=='/library/sections':return {'MediaContainer':{'Directory':[dict(key=2,type='show',title='Shows')]}}
            if url.path.endswith('/collections'):return {'MediaContainer':{'Metadata':[dict(ratingKey='9',type='collection',title='Collection')],'totalSize':51}}
            return {'MediaContainer':{'Metadata':[dict(ratingKey='42',type='episode',title='Episode',grandparentTitle='Show',viewOffset=42000,parentIndex=1,index=4)],'totalSize':1}}
        with patch.object(self.plex,'request',side_effect=request):
            self.plex.cache['42']=(999999999,{'stale':True})
            row=browse(self.plex,'plex','continue')['videos'][0]
            self.assertEqual(row['resume'],42);self.assertIn('S01E04',row['name'])
            self.assertNotIn('42',self.plex.cache)
            self.assertTrue(calls[-1].startswith('/hubs/continueWatching/items?'))
            browse(self.plex,'plex','unwatched','2')
            self.assertEqual(parse_qs(urlparse(calls[-1]).query)['unwatched'],['1'])
            self.assertEqual(parse_qs(urlparse(calls[-1]).query)['type'],['4'])
            browse(self.plex,'plex','recent','2')
            self.assertEqual(parse_qs(urlparse(calls[-1]).query)['sort'],['addedAt:desc'])
            listing=browse(self.plex,'plex','collections','2')
            self.assertEqual(listing['folders'][0]['path'],':plex:c9');self.assertEqual(listing['next'],50)
            with self.assertRaises(ValueError):browse(self.plex,'plex','unwatched','../../x')

    def test_jellyfin_resume_nextup_dedup_and_user_scope(self):
        calls=[]
        def request(path):
            calls.append(path);q=parse_qs(urlparse(path).query)
            self.assertEqual(q['UserId'],[A]);self.assertEqual(q['Limit'],['50'])
            rows=[dict(Id=B,Type='Episode',Name='Second',SeriesName='Anime',UserData={'PlaybackPositionTicks':730000000})]
            if path.startswith('/Shows/NextUp'):rows.append(dict(Id=P,Type='Episode',Name='Third'))
            return dict(Items=rows,TotalRecordCount=51)
        with patch.object(self.jf,'request',side_effect=request):
            page=browse(self.jf,'jellyfin','continue')
            self.assertEqual(len(page['videos']),2);self.assertEqual(page['videos'][0]['resume'],73)
            self.assertEqual(page['next'],50)
            browse(self.jf,'jellyfin','unwatched',P,50)
            query=parse_qs(urlparse(calls[-1]).query)
            self.assertEqual(query['IsPlayed'],['false']);self.assertEqual(query['StartIndex'],['50'])
            browse(self.jf,'jellyfin','collections')
            self.assertEqual(parse_qs(urlparse(calls[-1]).query)['IncludeItemTypes'],['BoxSet'])

    def test_shelf_episode_advances_in_its_own_season(self):
        rows=[dict(ratingKey='42',type='episode'),dict(ratingKey='43',type='episode')]
        def listing(kind,parent,offset=0):
            self.assertEqual((kind,parent),('m','8'));return dict(Metadata=rows[offset:])
        with patch.object(self.plex,'metadata',return_value=dict(type='episode',parentRatingKey='8')),patch.object(self.plex,'listing',side_effect=listing):
            result=self.plex.next_media(self.plex.token('42'))
            self.assertEqual(result['id'],self.plex.token('43.m8.1'))
        rows=[dict(Id=B,Type='Episode'),dict(Id=A,Type='Episode')]
        with patch.object(self.jf,'metadata',return_value=dict(Type='Episode',SeasonId=P)),patch.object(self.jf,'listing',side_effect=lambda kind,parent,offset=0:dict(Items=rows[offset:])):
            result=self.jf.next_media(self.jf.token(B))
            self.assertEqual(result['id'],self.jf.token(A,'m',P,1))

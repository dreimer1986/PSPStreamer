import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock
from psp_streamer.client_playback import PlaybackReports, report
from psp_streamer.season_next import provider_next, season_number
from psp_streamer.server import Library, MediaItem


class PlaybackTests(unittest.TestCase):
    def test_reports_are_isolated_ordered_and_validated(self):
        server=SimpleNamespace(library=Mock(),radio=Mock(),plex=Mock(),jellyfin=Mock(),comfort=Mock(),client_reports=PlaybackReports())
        data=dict(client='xbox-test',sequence=1,id='plex.test',state='playing',position=1000,duration=100000)
        self.assertTrue(report(server,data)['ok'])
        server.plex.report.assert_called_once_with('plex.test','playing',1000,100000,client='xbox-test')
        report(server,dict(data,sequence=3,state='stopped'))
        self.assertTrue(report(server,dict(data,sequence=2))['stale'])
        self.assertEqual(server.plex.report.call_count,2)
        report(server,dict(data,client='browser-test'))
        self.assertEqual(server.plex.report.call_count,3)
        for field,value in [('client','psp'),('position',float('nan')),('sequence',0),('state','finished')]:
            with self.assertRaises(ValueError):report(server,dict(data,**{field:value}))
        report(server,dict(data,client='browser-hex',id_hex=b'jellyfin.test'.hex()))
        self.assertEqual(server.jellyfin.report.call_count,1)

    def test_local_season_boundary_and_music_isolation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);library=Library([root])
            for name in ('Show/Season 1/E01.mkv','Show/Season 2/E01.mkv','Show/Season 10/E01.mkv','Show/Extras/bonus.mkv','Other/Season 1/E01.mkv','Show/Season 1/song.mp3','Show/Season 2/song.mp3'):
                p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.touch()
            token=lambda p:library.encode(MediaItem(0,p))
            first=token('Show/Season 1/E01.mkv');second=token('Show/Season 2/E01.mkv')
            self.assertEqual(library.next_media(first)['id'],second)
            self.assertEqual(library.next_media(second,previous=True)['id'],first)
            self.assertEqual(library.next_media(second)['id'],token('Show/Season 10/E01.mkv'))
            self.assertEqual(library.next_media(token('Show/Season 10/E01.mkv')), {})
            self.assertEqual(library.next_media(token('Show/Season 1/song.mp3')), {})
            self.assertEqual(library.next_media(first,shuffle=True),{})
            outside=Path(tmp).parent/'no-such-target'
            (root/'Show/Season 3').symlink_to(outside,target_is_directory=True)
            self.assertEqual(library.next_media(second)['id'],token('Show/Season 10/E01.mkv'))

    def test_provider_season_context_is_retained(self):
        for plex in (True,False):
            provider=Mock()
            provider.metadata.return_value=({'type':'episode','grandparentRatingKey':'show','parentIndex':1} if plex else
                {'Type':'Episode','SeriesId':'show','ParentIndexNumber':1})
            if plex:
                seasons=[dict(type='season',index=n,ratingKey=str(n)) for n in (1,2,10)]
                ep=dict(type='episode',ratingKey='ep',title='Episode')
                provider.listing.side_effect=lambda k,p,o=0:{'Metadata':seasons if p=='show' else [ep]}
            else:
                seasons=[dict(Type='Season',IndexNumber=n,Id=str(n)) for n in (1,2,10)]
                ep=dict(Type='Episode',Id='ep',Name='Episode')
                provider.listing.side_effect=lambda k,p,o=0:{'Items':seasons if p=='show' else [ep]}
            provider.token.return_value='next-token'
            self.assertEqual(provider_next(provider,'old',plex)['id'],'next-token')
            provider.token.assert_called_once_with(*(('ep.m2.0',) if plex else ('ep','m','2',0)))
        self.assertEqual(season_number('Staffel 02'),2)
        self.assertEqual(season_number('S03'),3)
        self.assertIsNone(season_number('Unrelated Show 4'))

if __name__=='__main__':unittest.main()

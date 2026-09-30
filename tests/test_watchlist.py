import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
from urllib.parse import parse_qs, urlparse
from psp_streamer.plex import Plex
from psp_streamer.watchlist import browse
from psp_streamer.provider_views import compact


class WatchlistTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.plex=Plex(self.tmp.name,[])
        self.addCleanup(self.plex.close)
        self.plex.config.update(enabled=True,watchlist=True,account='cloud-secret',server='local')

    def test_exact_guid_mapping_unavailable_no_cloud_ratingkeys_and_paging(self):
        cloud=[dict(type='show',guid='plex://show/show',title='Show',ratingKey='999'),
               dict(type='movie',guid='plex://movie/movie',title='Movie',ratingKey='888'),
               dict(type='movie',guid='plex://movie/missing',title='Missing')]
        def request(path, **kw):
            query=parse_qs(urlparse(path).query)
            if path.startswith('/library/sections/watchlist'):
                self.assertEqual(kw,dict(url='https://discover.provider.plex.tv',token='cloud-secret',timeout=5))
                return dict(MediaContainer=dict(Metadata=cloud,totalSize=23))
            self.assertEqual(kw,{'timeout':3})
            guid=query['guid'][0]
            row=dict(type='show' if guid.endswith('show') else 'movie',guid=guid,title='Local',ratingKey='42' if guid.endswith('show') else '43')
            if guid.endswith('missing'):row['guid']='plex://movie/different'
            return dict(MediaContainer=dict(Metadata=[row]))
        with patch.object(self.plex,'request',side_effect=request):
            result=browse(self.plex,10)
            self.assertEqual(result['next'],18)
            self.assertEqual(result['folders'][0]['path'],':plex:m42')
            self.assertEqual(result['videos'][0]['id'],self.plex.token('43'))
            self.assertEqual(result['unavailable'][0]['name'],'Missing')
            self.assertNotIn('cloud-secret',str(result))
            self.assertNotIn('999',str(result))
            page=compact(SimpleNamespace(plex=self.plex),':shelf:plex:watchlist::10')
            self.assertEqual(page['folders'][-1]['path'],':shelf:plex:watchlist::18')

    def test_optional_and_persistent_no_request_when_disabled(self):
        self.plex.configure_watchlist(dict(enabled=False))
        with patch.object(self.plex,'request') as request:
            with self.assertRaisesRegex(ValueError,'Enable Plex Watchlist'):
                browse(self.plex)
            request.assert_not_called()
        copy=Plex(self.tmp.name,[])
        self.addCleanup(copy.close)
        self.assertFalse(copy.public()['watchlist'])
        root=compact(SimpleNamespace(plex=self.plex),':shelf:plex')
        self.assertFalse(any('watchlist' in r['path'] for r in root['folders']))
        with self.assertRaises(ValueError):self.plex.configure_watchlist(dict(enabled='true'))


if __name__ == '__main__':
    unittest.main()

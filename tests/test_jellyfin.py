import json
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest.mock import patch
from urllib.request import Request, urlopen
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from psp_streamer.jellyfin import Jellyfin, authorization
from psp_streamer.server import Library

A, B, P = 'a'*32, 'b'*32, 'c'*32


class JellyfinTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.jf = Jellyfin(self.temp.name, [])
        self.addCleanup(self.jf.close)
        self.jf.config.update(enabled=True, token='private-token', user=A, server='test-server',
                              username='tester', url='http://example.invalid')

    def test_private_login_persistence_and_namespaces(self):
        with patch.object(self.jf, 'request', return_value={'AccessToken':'new-token',
                'User':{'Id':B}, 'ServerId':'server-two'}) as request:
            result = self.jf.login(dict(url='https://example.org/jellyfin/', username='user', password='secret'))
            self.assertTrue(result['enabled'])
            self.assertNotIn('token', result)
            self.assertEqual(request.call_args.kwargs['data']['Pw'], 'secret')
        content = self.jf.path.read_text()
        self.assertNotIn('secret', content)
        self.assertEqual(self.jf.path.stat().st_mode & 0o777, 0o600)
        token = self.jf.token(A,'m',P,99)
        self.assertEqual(self.jf.split(token), (A,'m',P,'99'))
        self.jf.config['user'] = A
        with self.assertRaises(ValueError): self.jf.split(token)
        for url in ('file:///etc', 'https://user:pass@host', 'https://host?token=x'):
            with self.assertRaises(ValueError): self.jf.login(dict(url=url, username='u', password='p'))
        with self.assertRaises(ValueError): authorization('client','bad"token')

    def test_browse_pages_metadata_and_next(self):
        episode = dict(Id=A, Type='Episode', Name='Übung', ParentIndexNumber=1, IndexNumber=2)
        with patch.object(self.jf, 'listing', return_value={'Items':[episode], 'TotalRecordCount':102}):
            page = self.jf.browse(0, ':jellyfin:m'+P+'@100')
            self.assertEqual(page['videos'][0]['name'],'S01E02 Übung')
            self.assertEqual(len(page['folders']),2)
            self.assertEqual(self.jf.split(page['videos'][0]['id']), (A,'m',P,'100'))
        with patch.object(self.jf, 'listing', side_effect=[{'Items':[episode]}, {'Items':[dict(Id=B,Type='Episode')]}]):
            self.assertEqual(self.jf.split(self.jf.next_media(self.jf.token(A,'m',P,0))['id'])[0],B)
        with patch.object(self.jf, 'listing', return_value={'Items':[dict(Id=B,Type='Episode')]}):
            self.assertEqual(self.jf.next_media(self.jf.token(A,'p',P,0)),{})
        with patch.object(self.jf, 'metadata', return_value=dict(episode, UserData=dict(Played=True,PlaybackPositionTicks=450000000))):
            details=self.jf.details(self.jf.token(A))
            self.assertEqual(details['resume'],45)
            self.assertTrue(details['watched'])

    def test_library_routes_music_without_filesystem_mount(self):
        library=Library([]);library.jellyfin=self.jf
        row=dict(Id=A,Type='Audio',Path='/Music/Übung.flac',Container='flac')
        with patch.object(self.jf,'metadata',return_value=row):
            item,source=library.decode(self.jf.token(A))
            self.assertEqual(source.name,'Übung.flac')
            self.assertNotIn('private-token',str(source))
        with patch.object(self.jf,'next_media',return_value={'id':'next','kind':'audio'}):
            self.assertEqual(library.next_media(self.jf.token(A)),{'id':'next','kind':'audio'})
        with self.assertRaises(ValueError):library.decode('jellyfin.invalid')

    def test_embedded_subtitle_ordinal_maps_to_api_stream_index(self):
        source=dict(Id=A,Path='/movie.mkv',MediaStreams=[
            dict(Type='Subtitle',Index=2,Codec='ass',IsExternal=True),
            dict(Type='Subtitle',Index=5,Codec='ass',IsExternal=False),
            dict(Type='Subtitle',Index=8,Codec='pgssub',IsExternal=False)])
        row=dict(Id=A,Path='/movie.mkv',MediaSources=[source])
        srt=b'1\n00:00:01,000 --> 00:00:02,000\nTest\n'
        with patch.object(self.jf,'metadata',return_value=row),patch.object(self.jf,'request',return_value=srt) as request:
            self.assertEqual(self.jf.text_subtitle(self.jf.token(A),0),srt.decode())
            self.assertTrue(request.call_args.args[0].endswith('/Subtitles/5/Stream.srt'))
            self.assertIsNone(self.jf.text_subtitle(self.jf.token(A),1))
            self.assertIsNone(self.jf.text_subtitle(self.jf.token(A),2))

    def test_reports_start_pause_resume_stop_in_ticks(self):
        calls=[]
        row=dict(Id=A,MediaSources=[dict(Id=A)])
        with patch.object(self.jf,'metadata',return_value=row), patch.object(self.jf,'request',
                side_effect=lambda path,**kw:calls.append((path,kw.get('data')))):
            for state,position,count in [('playing',1000,2),('paused',2000,3),('playing',2000,4),('stopped',3000,5)]:
                self.jf.report(self.jf.token(A),state,position,10000)
                limit=time.monotonic()+2
                while len(calls)<count and time.monotonic()<limit:time.sleep(.005)
                self.assertEqual(len(calls),count)
            self.jf.close()
        self.assertEqual(calls[0][0],'/Sessions/Playing')
        self.assertTrue(calls[2][1]['IsPaused'])
        self.assertEqual(calls[-1][0],'/Sessions/Playing/Stopped')
        self.assertEqual(calls[-1][1]['PositionTicks'],30000000)
        self.assertEqual(len({c[1]['PlaySessionId'] for c in calls}),1)

    def test_original_http_ranges_and_authorization(self):
        seen=[]
        class Handler(BaseHTTPRequestHandler):
            def log_message(self,*args):pass
            def do_GET(self):
                seen.append((self.path,self.headers.get('Authorization'),self.headers.get('Range')))
                self.send_response(206)
                self.send_header('Content-Length','4')
                self.send_header('Content-Range','bytes 4-7/16')
                self.end_headers();self.wfile.write(b'4567')
        server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
        worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
        try:
            self.jf.config['url']=f'http://127.0.0.1:{server.server_port}'
            with patch.object(self.jf,'metadata',return_value=dict(Id=A,Type='Movie',Path='/movies/movie.mkv')):
                source=self.jf.source(self.jf.token(A))
            with urlopen(Request(str(source),headers={'Range':'bytes=4-7'}),timeout=3) as response:
                self.assertEqual(response.status,206);self.assertEqual(response.read(),b'4567')
            self.assertEqual(seen[0][0],'/Items/'+A+'/Download')
            self.assertIn('Token="private-token"',seen[0][1])
            self.assertEqual(seen[0][2],'bytes=4-7')
            bridge=self.jf.media_bridge
            bridge.report_pause(A,True)
            self.assertTrue(bridge.paused(self.jf.config['url']+'/Items/'+A+'/Download'))
            bridge.report_pause(A,False)
            self.assertFalse(bridge.paused(self.jf.config['url']+'/Items/'+A+'/Download'))
            with self.assertRaises(ValueError):self.jf.media_bridge.source({'key':'/Items/'+A+'/Download?url=evil'},'x',0)
            self.jf.config['user']=B
            with self.assertRaises(ValueError):self.jf.media_bridge.resolve(str(source).split(str(self.jf.media_bridge.server_port),1)[1])
        finally:
            server.shutdown();server.server_close();worker.join()

    def test_playlist_pagination_and_source_switch_validation(self):
        with patch.object(self.jf,'request',return_value={'Items':[dict(Id=P,Name='Playlist')],
                                                        'TotalRecordCount':102}):
            page=self.jf.browse(0,':jellyfin:playlists@100')
            self.assertEqual(page['folders'][0]['path'],':jellyfin:p'+P)
            self.assertEqual(page['folders'][-1]['path'],':jellyfin:playlists@101')
        self.jf.additional_source=lambda:False
        with self.assertRaises(ValueError):self.jf.configure({'enabled':False})
        self.jf.additional_source=lambda:True
        self.jf.configure({'enabled':False})
        with self.assertRaises(ValueError):self.jf.browse(0,':jellyfin:')

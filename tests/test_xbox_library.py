"""Focused tests of Xbox adapters; no video stress/regression suite."""
import tempfile
import unittest
import struct
import http.client
import json
import os
import threading
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch
from psp_streamer.comfort import Comfort
from psp_streamer.playlist import Playlist
from psp_streamer.xbox_library import listing, change, next_media
from psp_streamer.artwork import Artwork
from psp_streamer.xbox_player import command


class XboxLibraryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.server = SimpleNamespace(comfort=Comfort(self.temp.name), playlist=Playlist(self.temp.name),
            playlist_item=lambda item:dict(id=item['id'],name=item.get('name') or item['id'],kind='video',audio=0,subtitle=-1))

    def action(self, action, **fields):
        return change(self.server, dict(action=action, revision=self.server.playlist.snapshot()['revision'], **fields))

    def test_favorites_queue_changes_and_persistence(self):
        name='Äpfel & Grüße'
        self.action('favorite',id_hex='one'.encode().hex(),name_hex=name.encode().hex(),value=True)
        self.assertEqual(listing(self.server,0,':xbox:favorites:',0)['entries'][0]['name'], name)
        self.action('add',id='one');self.action('add',id='two')
        self.action('move',id='two',position=0)
        self.action('enabled',enabled=True);self.action('repeat',repeat=2)
        self.assertEqual(self.server.playlist.next('one')['id'],'two')
        self.assertEqual(listing(self.server,0,':xbox:queue:',0)['entries'][0]['id'],'two')
        self.assertEqual(Playlist(self.temp.name).snapshot()['repeat'],2)
        with self.assertRaises(ValueError):change(self.server,dict(action='remove',id='one',revision=-1))
        self.action('remove',id='two')
        self.assertEqual(len(self.server.playlist.snapshot()['items']),1)
        self.action('favorite',id='one',value=False)
        self.assertEqual(listing(self.server,0,':xbox:favorites:',0)['entries'],[])

    def test_shared_search_keeps_folder_roots_and_status(self):
        self.server.search=Mock()
        self.server.search.snapshot.return_value=dict(results=[dict(id='series',name='Series',root=3,folder=1,audio=0)],running=True,limited=False,errors=[])
        result=listing(self.server,0,':xbox:search:',0,'Series')
        self.assertEqual(result['entries'][0]['root'],3)
        self.assertEqual(result['entries'][0]['kind'],'folder')
        self.assertEqual(result['running'],1)
        self.assertEqual(result['total'],1)

    def test_queue_repeat_precedes_folder_repeat_and_manual_skip(self):
        self.server.library=Mock()
        self.action('add',id='one');self.action('add',id='two')
        self.action('enabled',enabled=True)
        self.assertEqual(next_media(self.server,'one',repeat_one=True)['id'],'two')
        self.action('repeat',repeat=1)
        self.assertEqual(next_media(self.server,'one')['id'],'one')
        self.assertEqual(next_media(self.server,'one',manual=True)['id'],'two')
        self.action('enabled',enabled=False)
        self.assertEqual(next_media(self.server,'one',repeat_one=True)['id'],'one')
        self.server.library.next_media.assert_not_called()

    def test_hd_artwork_is_separate_and_bounded(self):
        art=Artwork(Mock())
        art.psp_identity=Mock(return_value=('token','scope',dict(backdrop='a',cover='b'),'tag'))
        art._psp_scope=Mock(return_value='scope')
        art._psp_plane=Mock(side_effect=lambda t,s,k,p,w,h: bytes(w*h*2))
        data=art.xbox('token')
        magic,bw,bh,cw,ch,bs,cs=struct.unpack('<4sHHHHII',data[:20])
        self.assertEqual((magic,bw,bh,cw,ch),(b'XART',1280,720,240,336))
        self.assertEqual(len(data),20+bs+cs)
        self.assertLess(len(data),2*1024*1024)
        self.assertEqual(bs,1280*720*2)

    def test_mp2_quality_whitelist_and_clock(self):
        base=['ffmpeg','-i','input','-c:a','libmp3lame','-ar','44100','-b:a','128k','-f','mp3','pipe:1']
        for rate in ('128k','192k','256k','320k','384k'):
            args=command(base,True,audio_quality=rate)
            self.assertEqual(args[args.index('-b:a')+1],rate)
            self.assertEqual(args[args.index('-ar')+1],'48000')
            self.assertEqual(args[args.index('-c:a')+1],'mp2')
        with self.assertRaises(ValueError):command(base,True,audio_quality='v5')

    def test_root_shortcuts_and_page_limits(self):
        with patch('psp_streamer.xbox_library.browse',return_value=dict(root=0,path='',parent=None,folders=[],videos=[])):
            result=listing(self.server,0,'',0)
        self.assertEqual([e['path'] for e in result['entries']],[':xbox:favorites:',':xbox:recent:',':xbox:queue:',':xbox:search:'])
        with self.assertRaises(ValueError):listing(self.server,0,'',-1)

    def test_real_http_adapters_and_xbox_status_without_psp_mutation(self):
        from psp_streamer.server import AppServer, Library, MediaItem
        root=Path(self.temp.name)/'media';root.mkdir();(root/'Song.mp3').touch()
        with patch.dict(os.environ,{'PSP_STREAMER_SETTINGS_DIR':self.temp.name,
            'PSP_STREAMER_DOWNLOAD_DIR':self.temp.name+'/downloads','PSP_STREAMER_PASSWORD':''}):
            server=AppServer(('127.0.0.1',0),Library([root]))
        thread=threading.Thread(target=server.serve_forever);thread.start()
        try:
            def request(path,body=None):
                conn=http.client.HTTPConnection(*server.server_address,timeout=5)
                conn.request('POST' if body is not None else 'GET',path,
                    json.dumps(body) if body is not None else None,{'Content-Type':'application/json'})
                response=conn.getresponse();data=json.loads(response.read());conn.close()
                self.assertEqual(response.status,200,data);return data
            token=server.library.encode(MediaItem(0,'Song.mp3'))
            queue=request('/api/xbox/library?path=:xbox:queue:')
            request('/api/xbox/library-action',dict(action='add',id_hex=token.encode().hex(),revision=queue['revision']))
            row=request('/api/xbox/library?path=:xbox:queue:')['entries'][0]
            self.assertEqual(row['id'],token)
            request('/api/xbox/library-action',dict(action='favorite',id_hex=token.encode().hex(),value=True))
            self.assertEqual(request('/api/xbox/library?path=:xbox:favorites:')['entries'][0]['id'],token)
            server.player_status.remember(token,{'name':'Song','artwork':{'cover':'/api/artwork/test/cover'}},'audio')
            server.xbox_remote.poll(0)
            request('/api/client-playback',dict(client='xbox-test',sequence=1,id=token,name='Song',kind='audio',state='playing',position=2000,duration=10000))
            status=request('/api/xbox/status')
            self.assertTrue(status['online']);self.assertEqual(status['server_id'],server.player_status.identity)
            self.assertEqual(status['metadata']['artwork']['cover'],'/api/artwork/test/cover')
            self.assertEqual(server.player_status.snapshot()['state'],'idle')
            request('/api/xbox/command',dict(action='play',id=token,xbox_audio='384k'))
            self.assertEqual(server.xbox_remote.poll(0)['xbox_audio'],'384k')
            self.assertEqual(server.remote_sequence,0)
        finally:
            server.shutdown();thread.join();server.server_close()


if __name__=='__main__':unittest.main()

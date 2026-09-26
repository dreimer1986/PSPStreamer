import http.client
import json
import os
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import patch

from psp_streamer.playlist import Playlist
from psp_streamer.server import AppServer, Library, MediaItem


class PlaylistTests(unittest.TestCase):
    def test_slow_provider_does_not_lock_playback_and_rechecks_revision(self):
        with tempfile.TemporaryDirectory() as directory:
            queue=Playlist(directory);entered=threading.Event();release=threading.Event();errors=[]
            def resolve(row):entered.set();release.wait(2);return row
            def add():
                try:queue.change(dict(action='add',revision=0,items=[dict(id='a')]),resolve)
                except ValueError as error:errors.append(str(error))
            worker=threading.Thread(target=add);worker.start()
            try:
                self.assertTrue(entered.wait(1))
                self.assertIsNone(queue.next('outside'))
                queue.change(dict(action='enabled',revision=0,enabled=True),dict)
            finally:release.set();worker.join(3)
            self.assertEqual(len(errors),1)
            self.assertEqual(queue.snapshot()['items'],[])

    def test_order_conflicts_removal_restart_and_folder_fallback(self):
        with tempfile.TemporaryDirectory() as directory:
            queue=Playlist(directory)
            def change(action,**values):
                return queue.change(dict(action=action,revision=queue.snapshot()['revision'],**values),dict)
            rows=[dict(id=x,name=x,kind='audio',audio=0,subtitle=-1) for x in 'abcd']
            change('add',items=rows+rows)
            self.assertEqual(len(queue.snapshot()['items']),4)
            self.assertIsNone(queue.next('a'))
            change('enabled',enabled=True)
            self.assertEqual(queue.next('a')['id'],'b')
            self.assertEqual(queue.next('b',True)['id'],'a')
            self.assertEqual(queue.next('d'),{})
            self.assertIsNone(queue.next('outside'))
            with self.assertRaises(ValueError):queue.change(dict(action='clear',revision=0),dict)
            change('remove',id='b')
            change('remove',id='a')
            self.assertEqual(queue.next('b')['id'],'c')
            queue=Playlist(directory)
            self.assertEqual(queue.next('b')['id'],'c')
            change('move',id='d',position=0)
            self.assertEqual([row['id'] for row in queue.snapshot()['items']],['d','c'])
            change('clear')
            self.assertEqual(queue.next('c'),{})
            self.assertEqual(queue.next('b'),{})

    def test_http_mixed_playlist_and_native_edits(self):
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ,{
            'PSP_STREAMER_STATE_DIR':directory,'PSP_STREAMER_SETTINGS_DIR':'',
            'PSP_STREAMER_LEGACY_STATE_DIR':'','PSP_STREAMER_RADIO_DIR':'',
            'PSP_STREAMER_DOWNLOAD_DIR':'','PSP_STREAMER_PASSWORD':'',
            'PSP_STREAMER_TLS_CERT':'','PSP_STREAMER_TLS_KEY':''}):
            root=Path(directory)
            for filename in ('song.mp3','episode.mkv'):(root/filename).touch()
            library=Library([root])
            song=library.encode(MediaItem(0,'song.mp3'))
            film=library.encode(MediaItem(0,'episode.mkv'))
            with AppServer(('127.0.0.1',0),library) as server:
                worker=threading.Thread(target=server.serve_forever);worker.start()
                connection=http.client.HTTPConnection(*server.server_address,timeout=3)
                def request(method,path,body=None,expected=200):
                    connection.request(method,path,None if body is None else json.dumps(body),{'Content-Type':'application/json'})
                    response=connection.getresponse();data=json.loads(response.read())
                    self.assertEqual(response.status,expected,data);return data
                try:
                    data=request('POST','/api/playlist',dict(action='add',revision=0,items=[dict(id=song),dict(id=film,audio=1,subtitle=0)]))
                    self.assertEqual([row['kind'] for row in data['items']],['audio','video'])
                    request('POST','/api/playlist',dict(action='play',revision=1,id=song))
                    self.assertEqual(server.remote_command['id'],song)
                    following=request('GET','/api/media-next/'+song)
                    self.assertEqual((following['id'],following['audio'],following['subtitle']),(film,1,0))
                    self.assertEqual(request('GET','/api/media-next/'+film),{})
                    listing=request('GET','/api/library?path=%3Aqueue%3A')
                    self.assertTrue(listing['enabled']);self.assertEqual(len(listing['videos']),2)
                    request('POST','/api/playlist',dict(action='move',revision=2,id_hex=film.encode().hex(),position=0))
                    self.assertEqual(request('GET','/api/media-next/'+film)['id'],song)
                    request('POST','/api/playlist',dict(action='clear',revision=0),400)
                    request('POST','/api/playlist',dict(action='remove',revision=3,id_hex=[]),400)
                finally:
                    connection.close();server.shutdown();worker.join()

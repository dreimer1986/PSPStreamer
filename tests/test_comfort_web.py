import tempfile
import time
import unittest
import subprocess
import base64
import http.client
import json
import os
import threading
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
from psp_streamer.comfort import Comfort
from psp_streamer.search import Search

class ComfortWebTests(unittest.TestCase):
    @unittest.skipUnless(os.environ.get('PLAYWRIGHT_MODULE'),'optional targeted browser check')
    def test_browser(self):
        from psp_streamer.server import AppServer,Library
        root=Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temp,patch.dict(os.environ,{
            'PSP_STREAMER_SETTINGS_DIR':temp,'PSP_STREAMER_DOWNLOAD_DIR':temp+'/downloads','PSP_STREAMER_PASSWORD':''}):
            media=Path(temp)/'media';(media/'Grüße Anime').mkdir(parents=True)
            with AppServer(('127.0.0.1',0),Library([media])) as server:
                thread=threading.Thread(target=server.serve_forever);thread.start()
                try:subprocess.run(['node',str(root/'tests/comfort_browser.cjs'),f'http://127.0.0.1:{server.server_port}'],check=True,timeout=40)
                finally:server.shutdown();thread.join()

    def test_authenticated_endpoints_and_native_json_post(self):
        from psp_streamer.server import AppServer,Library
        with tempfile.TemporaryDirectory() as temp,patch.dict(os.environ,{
            'PSP_STREAMER_SETTINGS_DIR':temp,'PSP_STREAMER_DOWNLOAD_DIR':temp+'/downloads','PSP_STREAMER_PASSWORD':'test-only'}):
            with AppServer(('127.0.0.1',0),Library([Path(temp)])) as server:
                thread=threading.Thread(target=server.serve_forever);thread.start()
                try:
                    def request(method,path,data=None,auth=True):
                        conn=http.client.HTTPConnection(*server.server_address,timeout=3)
                        headers={'Content-Type':'application/json'}
                        if auth:headers['Authorization']='Basic '+base64.b64encode(b'psp:test-only').decode()
                        conn.request(method,path,json.dumps(data) if data is not None else None,headers)
                        r=conn.getresponse();status=r.status;body=r.read();conn.close();return status,body
                    self.assertEqual(request('GET','/api/comfort',auth=False)[0],401)
                    status,body=request('POST','/api/comfort',dict(id='test',name='Song',action='favorite',value=True,audio=1))
                    self.assertEqual(status,200,body)
                    self.assertEqual(json.loads(request('GET','/api/comfort')[1])['records'][0]['name'],'Song')
                    status,body=request('POST','/api/comfort/sync',dict(client='a'*16,records=[]))
                    self.assertEqual(status,200,body);self.assertIn(b'[[',body)
                    self.assertEqual(request('GET','/api/search?q=Song')[0],200)
                finally:server.shutdown();thread.join()

    def test_native_sync_preserves_private_state_and_skips_playback(self):
        root=Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temp:
            (Path(temp)/'ms0:/PSP/SYSTEM').mkdir(parents=True)
            binary=Path(temp)/'sync'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=undefined',
                '-I',str(root/'psp-client'),str(root/'tests/comfort_sync_native.c'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],cwd=temp,check=True,timeout=3)

    def test_durable_shared_favorites_and_history(self):
        with tempfile.TemporaryDirectory() as folder:
            s=Comfort(folder)
            row=['media-id'.encode().hex(),'Grüße'.encode().hex(),0,0,1,60,1]
            data={'client':'1234567890abcdef','records':[row]}
            first=s.sync(data)['records'][0]
            s.change({'id':'media-id','action':'favorite','value':False})
            again=s.sync(dict(data,records=[first]))['records'][0]
            self.assertEqual(again[4],0)  # stale PSP must not resurrect web removal
            again[4]=1
            final=s.sync(dict(data,records=[again]))['records'][0]
            self.assertEqual(final[4],1)  # explicit PSP edit comes back to server
            s.change({'id':'media-id','action':'forget'})
            final=s.sync(dict(data,records=[final]))['records'][0]
            self.assertEqual(final[5:], [0,0])
            self.assertEqual(Comfort(folder).snapshot(),s.snapshot())

    def test_report_progress_completion_and_no_failed_start(self):
        with tempfile.TemporaryDirectory() as folder:
            s=Comfort(folder)
            q=dict(media=['movie'],state=['playing'],position=['30000'],duration=['100000'],started=['0'])
            s.report(q,{'title':'Episode'})
            self.assertFalse(s.snapshot()['records'])
            q['started']=['1'];s.report(q,{'title':'Episode'})
            self.assertEqual(s.snapshot()['records'][0]['seconds'],30)
            q.update(position=['100000'],state=['stopped']);s.report(q,{})
            self.assertEqual(Comfort(folder).snapshot()['records'][0]['seconds'],0)

    def test_sync_rejects_profiles_and_invalid_rows(self):
        with tempfile.TemporaryDirectory() as folder:
            s=Comfort(folder)
            for rows in [[['bad']], [["ff","aa",0,0,1,0,1]], [["61","62",0,0,2,0,1]]]:
                with self.assertRaises((ValueError,UnicodeError)):s.sync({'client':'a'*16,'records':rows})
            self.assertFalse(s.snapshot()['records'])

    def test_background_search_all_sources_and_casefold(self):
        def browse(server,root=0,path=''):
            if not path:return {'folders':[dict(name='Files',path=':files:'),dict(name='Plex',path=':plex:')]}
            return dict(root=0,folders=[],videos=[dict(id=path,name='Meine GRÜẞE',kind='audio')])
        with patch('psp_streamer.catalogue.browse',side_effect=browse):
            s=Search(SimpleNamespace());d=s.snapshot('grüsse')
            deadline=time.monotonic()+2
            while d['running'] and time.monotonic()<deadline:
                time.sleep(.01);d=s.snapshot('grüsse')
            self.assertFalse(d['running']);self.assertEqual(len(d['results']),2)
            self.assertTrue(all(r['audio'] for r in d['results']))

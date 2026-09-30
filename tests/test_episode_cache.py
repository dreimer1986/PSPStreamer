import json
import http.client
import os
import subprocess
import threading
import time
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from psp_streamer.comfort import Comfort
from psp_streamer.episode_cache import EpisodeCache
from psp_streamer.offline import OfflineQueue
from psp_streamer.plex import Plex
from psp_streamer.jellyfin import Jellyfin
from psp_streamer.series_preferences import SeriesPreferences


class EpisodeCacheTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        library = Mock()
        library.decode.return_value = (None, self.root / 'episode.mkv')
        self.queue = OfflineQueue(self.root / 'jobs', library, None, None, None)
        self.queue.root.mkdir()
        self.server = SimpleNamespace(offline=self.queue, library=library,
                                      series_preferences=SeriesPreferences(self.root), comfort=Comfort(self.root))
        self.cache = EpisodeCache(self.server, self.root)
        self.addCleanup(self.cache.close)
        self.start = patch.object(self.queue, 'start')
        self.start.start()
        self.addCleanup(self.start.stop)
        self.rule = dict(key='owner', id='first', scope='folder:test', name='Series', count=2,
                         audio=dict(language='jpn', title='Japanese'), subtitle=None,
                         profile='normal', audio_quality='160k', video_fps='20')
        self.cache.data.update(enabled=True, rules=[self.rule])

    def add(self, token, owner='owner'):
        return self.queue.add_many([dict(id=token)], cache_owner=owner)[0]

    def test_manual_reuse_pins_and_restart_preserves_ownership(self):
        cached = self.add('one')
        manual = self.queue.add(dict(id='one'))
        self.assertEqual(cached['job'], manual['job'])
        self.assertNotIn('cache_owner', manual)
        other = self.add('two')
        fresh = OfflineQueue(self.queue.root, self.server.library, None, None, None)
        self.assertNotIn('cache_owner', fresh.get(manual['job']))
        self.assertEqual(fresh.get(other['job'])['cache_owner'], 'owner')
        self.cache.data['enabled'] = False
        self.cache.refresh()
        self.assertIn(manual['job'], self.queue.jobs)
        self.assertNotIn(other['job'], self.queue.jobs)

    def test_refresh_replaces_only_obsolete_cache_and_does_not_retry_errors(self):
        old = self.add('old')
        manual = self.add('manual', None)
        with patch.object(self.cache, 'candidates', return_value=['next', 'last']), patch.object(self.cache, 'options', side_effect=lambda r,t:dict(id=t)):
            self.cache.refresh()
            self.assertNotIn(old['job'], self.queue.jobs)
            self.assertIn(manual['job'], self.queue.jobs)
            self.assertEqual({j['id'] for j in self.queue.jobs.values()}, {'manual','next','last'})
            job = next(j for j in self.queue.jobs.values() if j['id']=='next')
            job.update(state='error', error='Track unavailable')
            count = len(self.queue.jobs)
            self.cache.refresh()
            self.assertEqual(len(self.queue.jobs), count)
            self.assertEqual(self.cache.errors['owner'], 'Track unavailable')

    def test_limit_counts_only_owned_payload_and_preserves_error_manifest(self):
        self.cache.data['limit_mib'] = 1
        owned, manual = self.add('one'), self.add('two', None)
        path = self.queue.root / owned['job'] / 'video.part'
        path.write_bytes(b'x' * 1048576)
        (self.queue.root / manual['job'] / 'video.part').write_bytes(b'x' * 2000000)
        with self.assertRaisesRegex(ValueError, 'storage limit'):
            self.cache.guard(owned)
        with patch.object(self.cache, 'candidates', return_value=['one']):
            self.cache.refresh()
        self.assertFalse(path.exists())
        self.assertTrue((path.parent / 'job.json').exists())
        self.assertEqual(self.queue.get(owned['job'])['state'], 'error')
        self.assertTrue((self.queue.root / manual['job'] / 'video.part').exists())
        self.assertLess(self.cache.usage(), 1048576)

    def test_pinned_download_is_not_evicted_using_stale_snapshot(self):
        job = self.add('one')
        self.queue.pin(job['job'])
        self.cache.discard(job)
        self.assertIn(job['job'], self.queue.jobs)

    def test_symlink_cache_folder_is_never_cleaned(self):
        job = self.add('one')
        folder = self.queue.root / job['job']
        (folder / 'job.json').unlink()
        folder.rmdir()
        outside = self.root / 'originals'
        outside.mkdir()
        (outside / 'precious').write_text('original')
        folder.symlink_to(outside, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'Unsafe'):
            self.cache.discard(job)
        self.assertEqual((outside / 'precious').read_text(), 'original')

    def test_track_languages_resolve_per_episode_and_missing_track_is_error(self):
        self.rule['subtitle'] = dict(language='deu', title='Full')
        with patch('psp_streamer.server.AppHandler.metadata', return_value=dict(
                a=[dict(n='2', l='jpn', t='Japanese')], s=[dict(n='4', l='ger', t='Full')])):
            result = self.cache.options(self.rule, 'next')
            self.assertEqual((result['audio'], result['subtitle']), (2,4))
        with patch('psp_streamer.server.AppHandler.metadata', return_value=dict(a=[], s=[])):
            with self.assertRaisesRegex(ValueError, 'Preferred track missing'):
                self.cache.options(self.rule, 'next')

    def test_rule_validation_persistence_and_settings_change_replaces_policy(self):
        payload = dict(kind='video', name='Episode', a=[dict(n='0',l='jpn')], s=[])
        with patch.object(self.cache, 'start'), patch.object(self.server.series_preferences, 'context', return_value='folder:test'):
            self.cache.configure(dict(action='add', id='first', count=3), payload)
            first = self.cache.data['rules'][0]['key']
            self.cache.configure(dict(action='add', id='first', count=4), payload)
            self.assertNotEqual(first, self.cache.data['rules'][0]['key'])
            self.assertEqual(len(self.cache.data['rules']),1)
            self.cache.configure(dict(action='settings', enabled=True, limit_mib=512))
            self.assertEqual(json.loads(self.cache.path.read_text())['limit_mib'],512)
            with self.assertRaises(ValueError):
                self.cache.configure(dict(action='settings', enabled=True, limit_mib=0))

    def test_local_completed_state_survives_record_eviction_and_restart(self):
        c = self.server.comfort
        c.report(dict(media=['first'],state=['stopped'],started=['1'],position=['100000'],duration=['100000']),dict(title='Episode'))
        c.data['records'].clear()
        c.save()
        self.server.comfort = Comfort(self.root)
        self.server.library.next_media.side_effect=lambda t:{'id':{'first':'second','second':'third'}.get(t)}
        with patch.object(self.server.series_preferences, 'context', return_value='folder:test'):
            self.assertEqual(self.cache.candidates(self.rule), ['second','third'])

    def test_plex_and_jellyfin_use_fresh_provider_watched_flags(self):
        plex = Plex(self.root, [])
        self.addCleanup(plex.close)
        plex.config.update(enabled=True, server='test')
        self.server.plex = plex
        rule = dict(self.rule, id=plex.token('12'))
        rows = [dict(ratingKey=str(n),parentIndex=1,index=n,viewCount=int(n==3)) for n in (1,2,3,4)]
        with patch.object(self.server.series_preferences,'context',return_value=rule['scope']), patch.object(plex,'metadata',return_value=dict(grandparentRatingKey='9',parentIndex=1,index=2)), patch.object(plex,'request',return_value={'MediaContainer':{'Metadata':rows}}):
            self.assertEqual(self.cache.candidates(rule), [plex.token('2'),plex.token('4')])
        jf = Jellyfin(self.root, [])
        self.addCleanup(jf.close)
        jf.config.update(enabled=True,server='test',user='a'*32)
        self.server.jellyfin = jf
        rule['id']=jf.token('b'*32)
        rows=[dict(Id=str(n)*32,ParentIndexNumber=1,IndexNumber=n,UserData=dict(Played=n==3)) for n in (1,2,3,4)]
        with patch.object(self.server.series_preferences,'context',return_value=rule['scope']), patch.object(jf,'metadata',return_value=dict(SeriesId='c'*32,ParentIndexNumber=1,IndexNumber=2)), patch.object(jf,'request',return_value={'Items':rows}) as request:
            self.assertEqual(self.cache.candidates(rule), [jf.token('2'*32),jf.token('4'*32)])
            self.assertIn('EnableUserData=true', request.call_args.args[0])


class EpisodeCacheHTTPTests(unittest.TestCase):
    def test_real_conversion_routes_completion_refill_and_pin(self):
        from psp_streamer.server import AppServer, Library, MediaItem
        with tempfile.TemporaryDirectory() as temp, patch.dict(os.environ, {
                'PSP_STREAMER_SETTINGS_DIR': temp, 'PSP_STREAMER_DOWNLOAD_DIR':temp+'/cache',
                'PSP_STREAMER_PASSWORD':''}):
            root=Path(temp)/'media'
            root.mkdir()
            source=root/'Episode 1.mkv'
            subprocess.run(['ffmpeg','-v','error','-f','lavfi','-i','testsrc2=size=160x90:rate=20:duration=1',
                            '-f','lavfi','-i','sine=sample_rate=44100:duration=1','-c:v','libx264','-c:a','aac',str(source)],check=True)
            (root/'Episode 2.mkv').write_bytes(source.read_bytes())
            library=Library([root])
            with AppServer(('127.0.0.1',0),library) as server:
                thread=threading.Thread(target=server.serve_forever)
                thread.start()
                try:
                    def request(method,path,data=None):
                        conn=http.client.HTTPConnection(*server.server_address,timeout=10)
                        conn.request(method,path,json.dumps(data) if data is not None else None, {'Content-Type':'application/json','X-PSP-Web':'1'})
                        reply=conn.getresponse()
                        body=reply.read()
                        self.assertEqual(reply.status,200,body)
                        conn.close()
                        return json.loads(body)
                    token=library.encode(MediaItem(0,source.name))
                    request('POST','/api/offline/reserve',dict(action='add',id=token,count=1))
                    request('POST','/api/offline/reserve',dict(action='settings',enabled=True,limit_mib=128))
                    def ready():
                        end=time.monotonic()+15
                        while time.monotonic()<end:
                            jobs=request('GET','/api/offline/jobs')
                            if jobs and all(j['state']=='ready' for j in jobs):return jobs
                            self.assertFalse(any(j['state']=='error' for j in jobs),jobs)
                            time.sleep(.05)
                        self.fail('Reserve conversion timed out')
                    jobs=ready()
                    self.assertEqual(len(jobs),1)
                    self.assertIn('cache_owner',jobs[0])
                    server.comfort.report(dict(media=[token],state=['stopped'],started=['1'],position=['1000'],duration=['1000']),dict(title='Episode 1'))
                    request('POST','/api/offline/reserve',dict(action='refresh'))
                    end=time.monotonic()+15
                    while time.monotonic()<end:
                        jobs=ready()
                        if jobs[0]['id']!=token:break
                        time.sleep(.05)
                    self.assertNotEqual(jobs[0]['id'],token)
                    downloaded=request('GET','/api/offline/job/'+jobs[0]['job'])
                    self.assertNotIn('cache_owner',downloaded)
                    request('POST','/api/offline/reserve',dict(action='settings',enabled=False,limit_mib=128))
                    self.assertEqual(request('GET','/api/offline/reserve')['bytes'],0)
                    self.assertEqual(len(request('GET','/api/offline/jobs')),1)
                    server.plex.config.update(account='test',enabled=True,server='test')
                    self.assertTrue(request('POST','/api/plex/watchlist',dict(enabled=True))['watchlist'])
                    with patch('psp_streamer.watchlist.browse',return_value=dict(folders=[],videos=[],unavailable=[],next=None)):
                        self.assertEqual(request('GET','/api/provider-view?provider=plex&view=watchlist')['videos'],[])
                    if os.environ.get('PLAYWRIGHT_MODULE'):
                        subprocess.run(['node',str(Path(__file__).with_name('episode_cache_web.cjs')),
                                        f'http://127.0.0.1:{server.server_port}'],check=True,timeout=30)
                finally:
                    server.shutdown()
                    thread.join()


if __name__ == '__main__':
    unittest.main()

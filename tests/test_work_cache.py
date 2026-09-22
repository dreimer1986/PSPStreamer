"""Targeted bounds, concurrency and local-file invalidation checks."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess
import tempfile
import threading
import unittest
from unittest.mock import patch

from psp_streamer.work_cache import WorkCache
from psp_streamer import probe_cache


class WorkCacheTests(unittest.TestCase):
    def test_single_flight_and_eviction(self):
        cache=WorkCache(entries=2,jobs=1,timeout=2)
        entered,release=threading.Event(),threading.Event()
        calls=[]
        def load():
            calls.append(1);entered.set();self.assertTrue(release.wait(2));return 'value'
        with ThreadPoolExecutor(4) as pool:
            first=pool.submit(cache.get,'same',load)
            self.assertTrue(entered.wait(2))
            with self.assertRaises(ValueError):cache.get('different',load)
            others=[pool.submit(cache.get,'same',load) for _ in range(3)]
            release.set()
            self.assertEqual([first.result()]+[f.result() for f in others],['value']*4)
        self.assertEqual(len(calls),1);self.assertFalse(cache.pending)
        cache.get('two',lambda:2);cache.get('three',lambda:3)
        self.assertEqual(list(cache.values),['two','three'])

    def test_failure_retry_and_uncached_partial_result(self):
        cache=WorkCache()
        def fail():raise ValueError('conversion')
        with self.assertRaises(ValueError):cache.get('x',fail)
        self.assertFalse(cache.pending);self.assertFalse(cache.values)
        self.assertEqual(cache.get('x',lambda:1,cache_if=lambda _:False),1)
        self.assertEqual(cache.get('x',lambda:2),2)
        self.assertEqual(cache.get('x',lambda:3),2)

    def test_local_probe_reuse_file_changes_and_remote_bypass(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(probe_cache,'_cache',WorkCache()), \
                patch('psp_streamer.probe_cache.subprocess.run') as run:
            source=Path(directory)/'media.mkv';source.write_bytes(b'one')
            run.return_value=subprocess.CompletedProcess([],0,'{}','')
            probe_cache.probe(source,['-show_streams']);probe_cache.probe(source,['-show_streams'])
            self.assertEqual(run.call_count,1)
            source.write_bytes(b'different')
            probe_cache.probe(source,['-show_streams']);self.assertEqual(run.call_count,2)
            probe_cache.probe(source,['-show_format']);self.assertEqual(run.call_count,3)
            for _ in range(2):probe_cache.probe('https://provider/live',['-show_streams'])
            self.assertEqual(run.call_count,5)
            run.return_value=subprocess.CompletedProcess([],1,'','failed')
            for _ in range(2):probe_cache.probe(source,['-select_streams','s:0'])
            self.assertEqual(run.call_count,7)

    def test_concurrent_local_probe_runs_once(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(probe_cache,'_cache',WorkCache()), \
                patch('psp_streamer.probe_cache.subprocess.run') as run:
            source=Path(directory)/'media.mkv';source.write_bytes(b'test')
            entered,release=threading.Event(),threading.Event()
            def execute(*args,**kwargs):
                entered.set();self.assertTrue(release.wait(2));return subprocess.CompletedProcess([],0,'{}','')
            run.side_effect=execute
            with ThreadPoolExecutor(4) as pool:
                first=pool.submit(probe_cache.probe,source,['-show_streams'])
                self.assertTrue(entered.wait(2))
                others=[pool.submit(probe_cache.probe,source,['-show_streams']) for _ in range(3)]
                release.set()
                for f in [first,*others]:self.assertEqual(f.result().stdout,'{}')
            self.assertEqual(run.call_count,1)

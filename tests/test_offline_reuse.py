import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

from psp_streamer.offline import OfflineQueue


class OfflineReuseTests(unittest.TestCase):
    def test_reuse_states_variants_batch_and_missing_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            library = Mock()
            library.decode.return_value = (None, root / 'episode.mkv')
            queue = OfflineQueue(root, library, None, None, None)
            options = {'id': 'episode'}
            with patch.object(queue, 'start'):
                first, duplicate = queue.add_many([options, options])
                self.assertEqual(first['job'], duplicate['job'])
                self.assertEqual(len(queue.jobs), 1)
                key = first['job']
                self.assertEqual(queue.add(options)['job'], key)
                queue.jobs[key]['state'] = 'encoding'
                self.assertEqual(queue.add(options)['job'], key)
                variant = queue.add(dict(options, audio=1))
                self.assertNotEqual(variant['job'], key)
                folder = root / key
                files = []
                for name in ('episode.flv', 'subtitles.ovl', 'seek.idx'):
                    (folder / name).write_bytes(b'test')
                    files.append({'name': name, 'size': 4})
                queue.jobs[key].update(state='ready', files=files)
                self.assertEqual(queue.add(options)['job'], key)
                (folder / 'episode.flv').unlink()
                replacement = queue.add(options)
                self.assertNotEqual(replacement['job'], key)
                queue.jobs[replacement['job']]['state'] = 'cancelled'
                retry = queue.add(options)
                self.assertNotEqual(retry['job'], replacement['job'])
                queue.jobs[retry['job']]['state'] = 'error'
                self.assertNotEqual(queue.add(options)['job'], retry['job'])

    def test_concurrent_requests_share_one_job(self):
        from concurrent.futures import ThreadPoolExecutor
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            library = Mock()
            library.decode.return_value = (None, root / 'episode.mkv')
            queue = OfflineQueue(root, library, None, None, None)
            with patch.object(queue, 'start'), ThreadPoolExecutor(max_workers=4) as pool:
                jobs = list(pool.map(queue.add, [{'id': 'episode'}] * 16))
            self.assertEqual(len({job['job'] for job in jobs}), 1)
            self.assertEqual(len(queue.jobs), 1)

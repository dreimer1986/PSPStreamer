import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

from psp_streamer.catalogue import browse, folder_media
from psp_streamer.server import Library, MediaItem
from psp_streamer.offline import OfflineQueue


class CatalogueTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root/'Album').mkdir()
        (self.root/'Album'/'song.mp3').touch()
        (self.root/'Episode.mkv').touch()
        self.server = SimpleNamespace(library=Library([self.root]), plex=Mock(), radio=Mock())
        self.server.plex.public.return_value = dict(files=True, enabled=True, radio=True)

    def test_sources_and_parent_round_trip(self):
        overview = browse(self.server)
        self.assertEqual(overview, browse(self.server, path='.'))
        self.assertEqual([x['path'] for x in overview['folders']], [':files:', ':plex:', ':radio:'])
        roots = browse(self.server, path=':files:')
        self.assertEqual(roots['parent'], '')
        listing = browse(self.server, path=roots['folders'][0]['path'])
        self.assertEqual(listing['parent'], ':files:')
        self.assertEqual(listing['videos'][0]['name'], 'Episode.mkv')
        album = browse(self.server, path=listing['folders'][0]['path'])
        self.assertEqual(album['parent'], ':files:/0')
        self.assertEqual(browse(self.server, path=album['parent']), listing)
        with self.assertRaises(ValueError):
            browse(self.server, path=':files:0/../../')
        self.server.plex.public.return_value['files'] = False
        self.assertEqual(len(browse(self.server)['folders']), 2)
        with self.assertRaises(ValueError):
            browse(self.server, path=':files:0')

    def test_recursive_is_explicit_and_bounded(self):
        self.assertEqual(len(folder_media(self.server, 0, ':files:0')), 1)
        self.assertEqual(len(folder_media(self.server, 0, ':files:0', True)), 2)
        with self.assertRaises(ValueError):
            folder_media(self.server, 0, '')
        for index in range(128):
            (self.root/f'{index}.mp3').touch()
        with self.assertRaisesRegex(ValueError, '128'):
            folder_media(self.server, 0, ':files:0')

    def test_plex_pages_not_subfolders(self):
        def page(root, path):
            offset = int(path.split('@')[1]) if '@' in path else 0
            return dict(videos=[dict(id=str(offset), name=str(offset))], folders=[
                dict(path=':plex:m5'), dict(path=':plex:s1@0')]+(
                [dict(path=':plex:s1@100')] if not offset else []))
        self.server.plex.browse.side_effect = page
        self.assertEqual([x['id'] for x in folder_media(self.server, 0, ':plex:s1')], ['0', '100'])
        self.assertEqual([x['id'] for x in folder_media(self.server, 0, ':plex:s1@100')], ['0', '100'])

    def test_batch_validation_does_not_publish_partial_jobs(self):
        # No worker is needed to test publishing/rollback semantics.
        manager = OfflineQueue(self.root/'cache', self.server.library, None, None, None)
        manager.root.mkdir()
        token = self.server.library.encode(MediaItem(0, 'Episode.mkv'))
        with patch.object(manager, 'start'):
            with self.assertRaises(ValueError):
                manager.add_many([dict(id=token), dict(id='invalid')])
            self.assertFalse(manager.jobs)
            result = manager.add_many([dict(id=token), dict(id=token, subtitle=0)])
            self.assertEqual(len(result), 2)
            self.assertEqual(result[1]['subtitle'], 0)
            with patch.object(manager, '_save', side_effect=OSError('disk full')):
                with self.assertRaises(OSError):
                    manager.add_many([dict(id=token)])
            self.assertEqual(len(manager.jobs), 2)

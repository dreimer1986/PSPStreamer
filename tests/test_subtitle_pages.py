import json
import http.client
import subprocess
import tempfile
import threading
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

from psp_streamer.server import AppHandler, AppServer, Library, parse_srt_cues
from psp_streamer.subtitle_pages import display_timeline, subtitle_page

ROOT = Path(__file__).resolve().parents[1]


class SubtitlePagesTests(unittest.TestCase):
    def test_complete_timeline_and_seek(self):
        cues = [[i*1000, i*1000+900, str(i)] for i in range(15000)]
        payload = {'t': 'text', 'c': display_timeline(cues)}
        result, offset = [], 0
        while offset >= 0:
            page = subtitle_page(payload, offset)
            self.assertLessEqual(len(page['c']), 256)
            result.extend(page['c'])
            offset = page['next']
        self.assertEqual(result, cues)
        for at, expected in [(0, 0), (899, 0), (900, 1), (1000, 1), (14999999, 15000)]:
            self.assertEqual(subtitle_page(payload, at_ms=at)['offset'], expected)

    def test_overlap_merge_and_utf8(self):
        self.assertEqual(display_timeline([[0, 10, 'A'], [5, 15, 'B'], [15, 20, 'B']]),
                         [[0, 10, 'A'], [10, 20, 'B']])
        text = display_timeline([[0, 10, 'ä'*100]])[0][2]
        self.assertEqual(text, 'ä'*79)
        self.assertEqual(subtitle_page({'t': 'pgs', 'c': []}), {'t': 'pgs', 'c': []})

    def test_srt_no_longer_truncated(self):
        srt = '\n\n'.join(f'{i}\n00:00:00,000 --> 00:00:01,000\nText {i}' for i in range(1900))
        self.assertEqual(len(parse_srt_cues(srt, 1000)), 1900)

    def test_shared_sources_and_cache(self):
        srt = '1\n00:00:00,000 --> 00:00:01,000\nGrüße!\n'
        for token in ('file-token', 'plex.token', 'jellyfin.token'):
            server = SimpleNamespace(subtitle_cache={}, subtitle_cache_lock=threading.Lock(),
                library=SimpleNamespace(decode=Mock(return_value=(None, '/mounted/media.mkv'))),
                jellyfin=SimpleNamespace(text_subtitle=Mock(return_value=srt)))
            handler = SimpleNamespace(server=server, send_json=Mock())
            with patch('psp_streamer.server.subprocess.run', side_effect=[
                SimpleNamespace(returncode=0, stdout='ass\n'),
                SimpleNamespace(returncode=0, stdout=srt)]) as run:
                AppHandler.subtitles(handler, token, 0, paged=True)
                first = handler.send_json.call_args.args[0]
                self.assertEqual(first['c'], [[0, 1000, 'Grüße!']])
                self.assertEqual(first['paged'], 1)
                calls = run.call_count
                AppHandler.subtitles(handler, token, 0, paged=True, at_ms=1000)
                self.assertEqual(handler.send_json.call_args.args[0]['c'], [])
                self.assertEqual(run.call_count, calls)

    def test_client_page_parser(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory)/'parser'
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT/'psp-client'),
                str(ROOT/'tests/subtitle_page_parser.c'), '-o', str(binary)], check=True)
            payload = {'t': 'text', 'c': [[i*10, i*10+9, 'Grüße'] for i in range(600)]}
            for offset in (0, 256, 512, 600):
                page = subtitle_page(payload, offset)
                encoded = json.dumps(page, ensure_ascii=False, separators=(',', ':'))
                subprocess.run([str(binary)], input=encoded.encode(), check=True)
            bad = '{"t":"text","paged":1,"offset":0,"next":-1,"until":100,"c":[[0,101,"bad"]]}'
            self.assertNotEqual(subprocess.run([str(binary)], input=bad.encode()).returncode, 0)

    def test_http_page_routing_and_legacy_limit(self):
        with tempfile.TemporaryDirectory() as directory, patch.dict('os.environ', {'PSP_STREAMER_PASSWORD': ''}):
            server = AppServer(('127.0.0.1', 0), Library([Path(directory)]))
            thread = threading.Thread(target=server.serve_forever)
            thread.start()
            conn = http.client.HTTPConnection(*server.server_address, timeout=5)
            try:
                for token in ('files', 'plex.test', 'jellyfin.test'):
                    server.subtitle_cache[(token, 0, 1000)] = {'t': 'text',
                        'c': [[i*1000, i*1000+900, str(i)] for i in range(2000)]}
                    for query, expected in [('page=1&offset=1792', 208),
                                            ('page=1&at_ms=1999000', 1), ('timebase=ms', 1800)]:
                        conn.request('GET', f'/api/subtitles/{token}?track=0&{query}')
                        response = conn.getresponse()
                        self.assertEqual(response.status, 200)
                        self.assertEqual(len(json.loads(response.read())['c']), expected)
            finally:
                conn.close()
                server.shutdown()
                thread.join()
                server.server_close()

    def test_demux_drain_clock(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory)/'drain'
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT/'psp-client'),
                str(ROOT/'tests/pts_drain_clock.c'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_dense_offline_falls_back_to_burn_in(self):
        from psp_streamer.offline import OfflineQueue
        cues = [[i*1000, i*1000+900, str(i)] for i in range(961)]
        queue = SimpleNamespace(_capture=Mock(return_value=b'ignored'), parse_cues=Mock(return_value=cues))
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            burn = OfflineQueue._subtitles(queue, {'id': 'file-token', 'subtitle': 0, 'profile': 'normal'},
                folder/'source.mkv', {'streams': [{'codec_type': 'subtitle', 'codec_name': 'ass'}]}, folder)
            self.assertEqual(burn, 0)
            self.assertEqual(json.loads((folder/'subtitles.ovl').read_bytes()[8:])['c'], [])

import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock

from psp_streamer.server import AppHandler
from psp_streamer.track_labels import subtitle_labels


def track(title='', codec='ass', size=None, **flags):
    tags = {'language': 'ger', 'title': title}
    if size is not None:
        tags['NUMBER_OF_BYTES'] = str(size)
    return {'codec_type': 'subtitle', 'codec_name': codec, 'tags': tags, 'disposition': flags}


class TrackLabelTests(unittest.TestCase):
    def test_saved_web_selection_never_guesses_between_duplicates(self):
        source = (Path(__file__).resolve().parents[1]/'static/offline.js').read_text()
        subprocess.run(['node', '-e', source + '''
const assert=require('assert');
const select={value:'',options:[
 {value:'0',textContent:'ger #1 Forced ASS | Signs'},
 {value:'1',textContent:'ger #2 ASS | Full'}]};
assert.equal(restoreTrack(select,'ger German'),false);
assert.equal(select.value,'');
assert.equal(restoreTrack(select,'ger Full'),true);
assert.equal(select.value,'1');
assert.equal(restoreTrack(select,'ger #1 Forced ASS | Signs'),true);
assert.equal(select.value,'0');
'''], check=True, timeout=5)

    def test_identity_flags_and_utf8(self):
        rows = subtitle_labels([track('ä'*60, forced=1), track('ä'*60, hearing_impaired=1, default=1)])
        self.assertTrue(rows[0]['t'].startswith('#1 Forced ASS'))
        self.assertTrue(rows[1]['t'].startswith('#2 SDH Default ASS'))
        self.assertEqual([r['n'] for r in rows], ['0', '1'])
        for row in rows:
            self.assertLessEqual(len(row['t'].encode()), 40)
        self.assertEqual(subtitle_labels([track('Forced')])[0]['t'], 'ASS | Forced')

    def test_sizes_are_not_forced_detection(self):
        rows = subtitle_labels([track(size=50), track(size=500)])
        self.assertIn('smaller', rows[0]['t'])
        self.assertNotIn('Forced', rows[0]['t'])
        self.assertNotIn('smaller', rows[1]['t'])
        for pair in ([track(size=50), track(size=None)],
                     [track(size=50), track(codec='hdmv_pgs_subtitle', size=500)],
                     [track(size=50), track(size=50)]):
            self.assertFalse(any('smaller' in r['t'] for r in subtitle_labels(pair)))

    def test_missing_titles_stats_aliases_and_bad_values(self):
        other = track(size='not known')
        other['tags'].update(language='deu', **{'NUMBER_OF_FRAMES-eng': '123'})
        rows = subtitle_labels([{'codec_type': 'audio'}, track(size=-1), other])
        self.assertEqual([r['t'] for r in rows], ['#1 ASS', '#2 ASS'])
        self.assertNotIn('bytes', rows[0])
        self.assertNotIn('bytes', rows[1])
        self.assertEqual(rows[1]['entries'], 123)

    def test_actual_ffprobe_metadata_for_file_plex_and_jellyfin(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subs = root/'subs.srt'
            subs.write_text('1\n00:00:00,000 --> 00:00:00,500\nHello\n')
            movie = root/'fixture.mkv'
            subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', 'color=s=16x16:d=1',
                '-i', str(subs), '-map', '0:v', '-map', '1:s', '-map', '1:s',
                '-c:v', 'libx264', '-c:s', 'srt', '-metadata:s:s:0', 'language=ger',
                '-metadata:s:s:1', 'language=ger', '-metadata:s:s:0', 'NUMBER_OF_BYTES=100',
                '-metadata:s:s:1', 'NUMBER_OF_BYTES=1000', '-disposition:s:0', 'forced',
                '-disposition:s:1', 'hearing_impaired', str(movie)], check=True, timeout=15)
            details = SimpleNamespace(details=Mock(return_value={}))
            server = SimpleNamespace(metadata_cache={}, player_status=Mock(), library=SimpleNamespace(decode=Mock(return_value=(None, movie))),
                                     plex=details, jellyfin=details)
            handler = SimpleNamespace(server=server, send_json=Mock())
            for token in ('file', 'plex.item', 'jellyfin.item'):
                AppHandler.metadata(handler, token)
                rows = handler.send_json.call_args.args[0]['s']
                self.assertEqual(rows[0]['t'], '#1 Forced smaller SRT')
                self.assertEqual(rows[1]['t'], '#2 SDH SRT')
                self.assertEqual(rows[0]['bytes'], 100)

import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
from psp_streamer.plex import Plex
from psp_streamer.jellyfin import Jellyfin
from psp_streamer.media_versions import selected, version_id, browse_versions
from psp_streamer.external_subtitles import tracks, payload
from psp_streamer.track_labels import subtitle_labels
from psp_streamer.server import ffmpeg_command

A,B,C='a'*32,'b'*32,'c'*32


class VersionTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.plex=Plex(self.temp.name,[]);self.addCleanup(self.plex.close)
        self.plex.config.update(enabled=True,token='test-plex',url='http://example.invalid')
        self.jf=Jellyfin(self.temp.name,[]);self.addCleanup(self.jf.close)
        self.jf.config.update(enabled=True,token='test-jf',url='http://example.invalid',user=A)
        self.library=SimpleNamespace(plex=self.plex,jellyfin=self.jf)

    def test_plex_versions_stable_after_reordering_and_reject_removed(self):
        versions=[{'id':1,'Part':[{'key':'/library/parts/1/file.mkv','file':'/tv/a.mkv'}]},
                  {'id':2,'Part':[{'key':'/library/parts/2/file.mkv','file':'/tv/b.mkv'}]}]
        row=dict(type='episode',ratingKey='42',Media=versions)
        with patch.object(self.plex,'metadata',return_value=row):
            listing=browse_versions(self.plex,self.plex.token('42.m20.0'),0)
            token=listing['videos'][1]['id']
            self.assertEqual(self.plex.split(token),('42','m','20','0'))
            source=self.plex.source(token);self.assertEqual(source.name,'b.mkv')
            versions.reverse();self.assertEqual(selected(self.plex,token)['id'],2)
            versions.pop(0)
            with self.assertRaises(ValueError):self.plex.source(token)

    def test_jellyfin_version_static_stream_and_external_index(self):
        chosen={'Id':C,'Path':'/tv/second.mkv','MediaStreams':[
            {'Type':'Subtitle','Index':2,'Codec':'subrip','Language':'eng'},
            {'Type':'Subtitle','Index':7,'Codec':'ass','Language':'deu','IsExternal':True,'DisplayTitle':'Deutsch'}]}
        row={'Id':A,'Type':'Episode','Path':'/tv/first.mkv','MediaSources':[{'Id':B,'Path':'/tv/first.mkv'},chosen]}
        token=self.jf.token(A)+'~'+version_id(chosen,True)
        body=b'1\n00:00:01,000 --> 00:00:02,000\nExternal\n'
        with patch.object(self.jf,'metadata',return_value=row),patch.object(self.jf,'request',return_value=body) as request:
            source=self.jf.source(token)
            url=self.jf.media_bridge.resolve(source.url.split(str(self.jf.media_bridge.server_port),1)[1])[0]
            self.assertIn('/Videos/'+A+'/stream?static=true&MediaSourceId='+C,url)
            probe={'streams':[{'codec_type':'subtitle','codec_name':'subrip'}]}
            external=tracks(self.library,token)
            labels=subtitle_labels(probe['streams']+external)
            self.assertEqual(labels[1]['n'],'1');self.assertEqual(labels[1]['l'],'deu')
            self.assertIn('External',labels[1]['t'])
            self.assertIsNone(payload(self.library,token,0,probe=probe))
            self.assertEqual(payload(self.library,token,1,probe=probe),('subrip',body))
            request.assert_called_with(f'/Videos/{A}/{C}/Subtitles/7/Stream.srt',raw=True,timeout=120)

    def test_jellyfin_external_pgs_requests_raw_pgssub(self):
        source={'Id':C,'MediaStreams':[{'Type':'Subtitle','Index':9,
            'Codec':'pgssub','IsExternal':True,'Path':'/tv/episode.de.sup'}]}
        row={'Id':A,'MediaSources':[source]}
        token=self.jf.token(A)
        with patch.object(self.jf,'metadata',return_value=row),patch.object(self.jf,'request',return_value=b'PG') as request:
            self.assertEqual(payload(self.library,token,0,probe={'streams':[]}),('hdmv_pgs_subtitle',b'PG'))
            request.assert_called_once_with(f'/Videos/{A}/{C}/Subtitles/9/Stream.pgssub',raw=True,timeout=120)

    def test_plex_external_tracks_raw_endpoint_and_unsupported(self):
        row={'Media':[{'id':9,'Part':[{'Stream':[
            {'streamType':3,'codec':'srt','index':2},
            {'streamType':3,'codec':'srt','id':42,'key':'/library/streams/42','languageCode':'deu'}]}]}]}
        with patch.object(self.plex,'metadata',return_value=row),patch.object(self.plex,'request',return_value=b'cue') as request:
            token=self.plex.token('10')
            probe={'streams':[{'codec_type':'subtitle'}]}
            self.assertEqual(payload(self.library,token,1,probe=probe),('subrip',b'cue'))
            request.assert_called_once_with('/library/streams/42',raw=True)
            row['Media'][0]['Part'][0]['Stream'][1].update(id=43,key='https://evil.invalid/file')
            with self.assertRaises(ValueError):payload(self.library,token,1,probe=probe)

    def test_external_burn_uses_sidecar_not_embedded_index(self):
        command=ffmpeg_command(Path('/video.mkv'),0,'flv',subtitle_track=3,
            subtitle_source=Path('/tmp/subtitle.srt'),external_subtitle=True)
        self.assertIn("subtitles='/tmp/subtitle.srt':si=0",command[command.index('-vf')+1])
        command=ffmpeg_command(Path('/video.mkv'),0,'flv',subtitle_track=3,
            subtitle_source=Path('/tmp/subtitle.sup'),bitmap_subtitle=True,external_subtitle=True,start_seconds=30)
        self.assertIn('[0:v:0][1:s:0]',command[command.index('-filter_complex')+1])
        self.assertEqual(command.count('-i'),2);self.assertEqual(command.count('-ss'),2)

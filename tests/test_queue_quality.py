from pathlib import Path
from types import SimpleNamespace
import subprocess
import tempfile
import unittest
from unittest.mock import Mock, patch
from email.message import Message
import io
import json
import threading

from psp_streamer.playlist import Playlist, quality_options, media_duration
from psp_streamer.server import AppServer, AppHandler

ROOT = Path(__file__).resolve().parents[1]


class QueueQualityTests(unittest.TestCase):
    def test_real_handlers_pass_quality_to_remote_and_next(self):
        with tempfile.TemporaryDirectory() as directory:
            server=AppServer.__new__(AppServer)
            server.playlist=Playlist(directory)
            server.player_status=SimpleNamespace(snapshot=lambda:dict(online=True,id='a'))
            server.library=SimpleNamespace(decode=lambda token:(0,Path(token+'.mkv')))
            server.remote_lock=threading.Lock();server.remote_sequence=0;server.remote_session='test'
            handler=AppHandler.__new__(AppHandler);handler.server=server
            handler.web_csrf=None
            handler.authorized=lambda:True
            replies=[];handler.send_json=lambda body,status=200:replies.append((body,status))
            def post(body):
                body['revision']=server.playlist.snapshot()['revision']
                data=json.dumps(body).encode();handler.rfile=io.BytesIO(data)
                handler.headers=Message();handler.headers['Content-Type']='application/json'
                handler.headers['Content-Length']=str(len(data));handler.path='/api/playlist'
                handler.do_POST();self.assertEqual(replies[-1][1],200,replies[-1][0])
                return replies[-1][0]
            post(dict(action='add',items=[dict(id='a',audio_quality='v4',video_fps='20'),dict(id='b')]))
            post(dict(action='quality',id='b',audio_quality='96k',video_fps='24000/1001'))
            post(dict(action='play',id='a'))
            self.assertEqual(server.remote_command['audio_quality'],'v4')
            self.assertEqual(server.remote_command['queue_entry'],1)
            handler.path='/api/media-next/a';handler.do_GET()
            self.assertEqual(replies[-1][0]['audio_quality'],'96k')
            self.assertEqual(replies[-1][0]['video_fps'],'24000/1001')
            handler.path='/api/playlist';handler.do_GET()
            self.assertEqual(replies[-1][0]['next']['id'],'b')

    def test_lto_shape_guard_keeps_valid_geometry(self):
        harness=r'''
#include <assert.h>
#include <math.h>
#include <string.h>
#include "milkdrop_decor.h"
int main(void){
    MdVertex v[MD_SHAPE_SIDES+2];MdShape p={0};p.tex_zoom=1;p.rad=.25f;
    for(int sides=3;sides<=MD_SHAPE_SIDES;sides++){
        p.sides=sides;assert(md_shape_vertices(v,&p,1)==sides+2);
        assert(!memcmp(&v[1],&v[sides+1],sizeof(v[1])));
    }
    p.sides=NAN;assert(md_shape_vertices(v,&p,1)==0);
    p.sides=3;p.tex_zoom=0;assert(md_shape_vertices(v,&p,1)==0);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'shape'
            subprocess.run(['cc','-x','c','-','-std=c11','-O3','-flto','-Wall','-Wextra','-Werror','-I',str(ROOT/'psp-client'),str(ROOT/'psp-client/milkdrop_decor.c'),'-lm','-o',str(binary)],input=harness,text=True,check=True)
            subprocess.run([str(binary)],check=True,timeout=5)

    def test_quality_persists_and_inherit_removes_override(self):
        with tempfile.TemporaryDirectory() as directory:
            queue = Playlist(directory)
            def change(action, **kw):
                return queue.change(dict(action=action, revision=queue.snapshot()['revision'], **kw), dict)
            change('add', items=[dict(id='a',kind='video',audio=2,subtitle=1),dict(id='b',kind='audio')])
            change('enabled', enabled=True)
            change('quality',id='a',audio_quality='v5',video_fps='24000/1001')
            saved = Playlist(directory).snapshot()['items'][0]
            self.assertEqual((saved['audio_quality'],saved['video_fps'],saved['audio'],saved['subtitle']),('v5','24000/1001',2,1))
            change('quality',id='b',audio_quality='96k',video_fps='20')
            self.assertEqual(queue.next('a')['audio_quality'],'96k')
            self.assertNotIn('video_fps',queue.next('a'))
            revision=queue.snapshot()['revision']
            with self.assertRaises(ValueError):change('quality',id='a',audio_quality='999k')
            self.assertEqual(queue.snapshot()['revision'],revision)
            change('quality',id='a',audio_quality='',video_fps='')
            self.assertNotIn('audio_quality',queue.snapshot()['items'][0])

    def test_resolution_validates_quality_and_preview_is_read_only(self):
        for invalid in ('320k',4,{},True):
            with self.assertRaises(ValueError):quality_options({'audio_quality':invalid})
        server=SimpleNamespace(library=SimpleNamespace(decode=lambda _: (0,Path('song.mp3'))))
        row=AppServer.playlist_item(server,dict(id='song',audio_quality='v4',video_fps='20'))
        self.assertEqual(row['audio_quality'],'v4');self.assertNotIn('video_fps',row)
        with tempfile.TemporaryDirectory() as directory:
            server.playlist=Playlist(directory)
            server.player_status=SimpleNamespace(snapshot=lambda:dict(online=True,id='a'))
            server.playlist.change(dict(action='add',revision=0,items=[dict(id='a'),dict(id='b')]),dict)
            server.playlist.change(dict(action='enabled',revision=1,enabled=True),dict)
            self.assertEqual(AppServer.playlist_web_snapshot(server)['next']['id'],'b')
            self.assertEqual(server.playlist.snapshot()['revision'],2)

    def test_duration_providers_probe_and_unknown(self):
        server=SimpleNamespace(plex=Mock(),jellyfin=Mock(),library=Mock())
        server.plex.metadata.return_value={'duration':120000}
        server.jellyfin.metadata.return_value={'RunTimeTicks':900000000}
        self.assertEqual(media_duration(server,'plex.a'),120)
        self.assertEqual(media_duration(server,'jellyfin.a'),90)
        server.library.decode.return_value=(0,Path('file.mkv'))
        with patch('psp_streamer.probe_cache.probe') as probe:
            probe.return_value=SimpleNamespace(returncode=0,stdout='{"format":{"duration":"12.5"}}')
            self.assertEqual(media_duration(server,'file'),12.5)
            self.assertEqual(probe.call_args.kwargs['timeout'],5)
            probe.return_value.stdout='{"format":{"duration":"NaN"}}'
            self.assertIsNone(media_duration(server,'file'))
            probe.side_effect=subprocess.TimeoutExpired('ffprobe',5)
            self.assertIsNone(media_duration(server,'file'))

    def test_psp_scoped_defaults_and_tls_buffer(self):
        harness=r'''
#include <assert.h>
#include "queue_quality.h"
#include "tls_read_buffer.h"
static unsigned char input[8192];
static size_t cursor;static int calls,fault;
static int read_raw(void *ctx,unsigned char *out,size_t size){
    (void)ctx;calls++;if(fault)return fault;
    if(size>sizeof(input)-cursor)size=sizeof(input)-cursor;
    memcpy(out,input+cursor,size);cursor+=size;return (int)size;
}
int main(void){
    QueueQuality scope={0};int q=2,f=0;
    assert(queue_quality_index("v5")==4);assert(queue_quality_index("invalid")==-1);
    queue_quality_apply(&scope,&q,&f,4,1);assert(q==4&&f==1);
    queue_quality_apply(&scope,&q,&f,-1,-1);assert(q==2&&f==0);
    queue_quality_apply(&scope,&q,&f,0,1);q=6; /* Current-only UI override. */
    queue_quality_restore(&scope,&q,&f);assert(q==2&&f==0&&!scope.active);
    TlsReadBuffer b={0};unsigned char out[8192];
    for(unsigned i=0;i<sizeof(input);i++)input[i]=(unsigned char)(i%251);
    assert(tls_read_buffer(&b,read_raw,0,out,5)==5&&calls==1);
    assert(tls_read_buffer(&b,read_raw,0,out+5,2043)==2043&&calls==1);
    assert(tls_read_buffer(&b,read_raw,0,out+2048,6144)==6144&&calls==2);
    assert(!memcmp(out,input,sizeof(out)));
    assert(tls_read_buffer(&b,read_raw,0,out,5)==0); /* EOF */
    fault=-123;assert(tls_read_buffer(&b,read_raw,0,out,5)==-123);
    assert(tls_read_buffer(&b,read_raw,0,out,100)==-123);
    int old=calls;assert(tls_read_buffer(&b,read_raw,0,out,0)==0&&calls==old);
    fault=0;cursor=8190;
    assert(tls_read_buffer(&b,read_raw,0,out,5)==2);
    assert(out[0]==input[8190]&&out[1]==input[8191]);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'queue-tls'
            subprocess.run(['cc','-x','c','-','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'psp-client'),'-o',str(binary)],input=harness,text=True,check=True)
            subprocess.run([str(binary)],check=True,timeout=5)

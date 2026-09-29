import io
import json
import tempfile
import subprocess
import unittest
from email.message import Message
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

from psp_streamer.playlist import Playlist
from psp_streamer.provider_views import compact
from psp_streamer.series_preferences import SeriesPreferences
from psp_streamer.server import AppHandler


class ComfortAdditions(unittest.TestCase):
    def test_actual_countdown_cancel_start_and_remote_precedence(self):
        root=Path(__file__).resolve().parents[1]
        source=(root/'psp-client/main.c').read_text()
        begin=source.index('static int episode_countdown(const char *name,int local) {')
        end=source.index('\n#include "offline_ui.h"',begin)
        harness=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct { unsigned Buttons; } SceCtrlData;
enum {PSP_CTRL_CIRCLE=1,PSP_CTRL_START=2,PSP_CTRL_CROSS=4,
      TXT_VIDEO,TXT_EPISODE_COUNTDOWN,TXT_EPISODE_COUNTDOWN_HELP};
static int next_episode_seconds,remote_control_sequence,mode,polls,requests,restores;
static unsigned long long clock_tick;
static char response[80];
static void keep_awake(void) {}
static void ui_restore_after_playback(void) {restores++;}
static unsigned long long sceKernelGetSystemTimeWide(void){return clock_tick;}
static void sceKernelDelayThread(int n){clock_tick+=n;assert(clock_tick<40000000);}
static void sceCtrlReadBufferPositive(SceCtrlData *p,int n){(void)n;p->Buttons=0;
    if(++polls==3)p->Buttons=mode==1?PSP_CTRL_CIRCLE:mode==2?PSP_CTRL_CROSS:mode==3?PSP_CTRL_START:0;}
static const char *tr(int id){return id==TXT_EPISODE_COUNTDOWN?"Next in %d":"text";}
static void settings_shell(const char *s){(void)s;}
static void settings_line(int row,int selected,const char *s){(void)row;(void)selected;(void)s;}
static void settings_help(const char *s){(void)s;}
static int media_request_get(const char *p,char *out,int n,int timeout,int extra){
    (void)p;(void)n;(void)timeout;(void)extra;requests++;strcpy(out,mode==4?"stop":"idle");return mode==5?-1:1;}
static int json_value(const char *in,const char *key,char *out,int n){(void)key;(void)n;strcpy(out,in);return 1;}
/* COUNTDOWN */
int main(void){
    assert(episode_countdown("Next",0)==1 && !requests && !restores);
    next_episode_seconds=2;
    for(mode=0;mode<6;mode++){
        clock_tick=0;polls=requests=0;
        int result=episode_countdown("Next",0);
        assert(result==(mode==0||mode==2));
        if(mode==0)assert(clock_tick==2000000);
        else assert(clock_tick<2000000);
    }
    mode=4;clock_tick=0;polls=requests=0;
    assert(episode_countdown("Local",1)==1 && requests==0);
}
'''.replace('/* COUNTDOWN */',source[begin:end])
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'countdown.c';path.write_text(harness)
            binary=Path(directory)/'countdown'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(path),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=5)

    def test_play_next_order_repeat_shuffle_and_persistence(self):
        for shuffle in (False,True):
            with self.subTest(shuffle=shuffle),tempfile.TemporaryDirectory() as directory:
                queue=Playlist(directory)
                def change(action,**fields):
                    return queue.change(dict(action=action,revision=queue.snapshot()['revision'],**fields),dict)
                change('add',items=[dict(id=x) for x in 'abcd'])
                change('shuffle',shuffle=shuffle)
                change('repeat',repeat=1)
                before=Playlist._ordered(queue.snapshot())
                anchor,target=before[0]['id'],before[-1]['id']
                change('play_next',items=[dict(id=target)],after=anchor)
                self.assertEqual(queue.next(anchor)['id'],target)
                self.assertEqual(queue.next(anchor)['id'],target)  # Lookup does not consume.
                queue=Playlist(directory)
                self.assertEqual(queue.next(anchor)['id'],target)
                queue.observe(target)
                self.assertNotIn('up_next',Playlist(directory).snapshot())
                change('repeat',repeat=0)
                visited=[anchor]
                while (row:=queue.next(visited[-1])):
                    self.assertNotIn(row['id'],visited)
                    visited.append(row['id'])
                expected=[r['id'] for r in before if r['id']!=target]
                expected.insert(1,target)
                self.assertEqual(visited,expected)

    def test_play_next_outside_queue_is_atomic(self):
        with tempfile.TemporaryDirectory() as directory:
            queue=Playlist(directory)
            request=dict(action='play_next',revision=0,after='a',items=[dict(id='b')])
            queue.change(request,dict)
            self.assertEqual(queue.next('a')['id'],'b')
            previous=queue.snapshot()
            with self.assertRaises(ValueError):
                queue.change(dict(action='play_next',revision=1,after='x',items=[dict(id='x')]),dict)
            self.assertEqual(queue.snapshot(),previous)

    def test_compact_shelves_and_pagination(self):
        provider=SimpleNamespace(require=Mock())
        server=SimpleNamespace(plex=provider,jellyfin=provider)
        for name in ('plex','jellyfin'):
            base=':shelf:'+name
            self.assertEqual(len(compact(server,base)['folders']),4)
            with patch('psp_streamer.provider_views.sections',return_value=[dict(id='123',name='Shows')]):
                self.assertEqual(compact(server,base+':recent')['folders'][0]['path'],base+':recent:123')
            with patch('psp_streamer.provider_views.browse',return_value=dict(folders=[],videos=[dict(id='episode')],next=50)) as browse:
                page=compact(server,base+':continue')
                self.assertEqual(page['folders'][0]['path'],base+':continue::50')
                compact(server,page['folders'][0]['path'])
                self.assertEqual(browse.call_args.args[-1],50)
                self.assertEqual(page['parent'],base)
        with self.assertRaises(ValueError):compact(server,':shelf:other')

    def test_series_language_title_persistence_and_off(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            store=SeriesPreferences(root)
            provider=SimpleNamespace(config=dict(url='https://example',user='user'),
                                     metadata=lambda token:dict(SeriesId='show'))
            server=SimpleNamespace(jellyfin=provider)
            payload=dict(kind='video',a=[dict(n='0',l='ja',t='Original'),dict(n='1',l='ger',t='Dub')],
                         s=[dict(n='0',l='deu',t='Signs'),dict(n='1',l='deu',t='Full')])
            store.save(server,'jellyfin.ep1',payload,dict(audio=0,subtitle=1))
            next_episode=dict(kind='video',a=list(reversed(payload['a'])),s=[dict(n='0',l='de',t='Full'),dict(n='1',l='de',t='Signs')])
            next_episode['a']=[dict(r,n=str(i)) for i,r in enumerate(next_episode['a'])]
            store=SeriesPreferences(root)
            result=store.attach(server,'jellyfin.ep2',next_episode)
            self.assertEqual((result['preferred_audio'],result['preferred_subtitle']),(1,0))
            provider.config['enabled']=False  # Harmless settings changes preserve preferences.
            self.assertEqual(store.attach(server,'jellyfin.ep2',payload)['series_saved'],1)
            provider.config['user']='other'
            self.assertEqual(store.attach(server,'jellyfin.ep2',payload)['series_saved'],0)
            provider.config['user']='user'
            store.save(server,'jellyfin.ep1',payload,dict(audio=1,subtitle=-1))
            self.assertEqual(store.attach(server,'jellyfin.ep2',next_episode)['preferred_subtitle'],-1)
            result=store.save(server,'jellyfin.ep1',payload,dict(remove=True))
            self.assertEqual(result['series_saved'],0)

    def test_metadata_cache_preferences_are_not_stale(self):
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'episode.mkv';source.touch()
            server=SimpleNamespace(library=SimpleNamespace(decode=lambda token:(0,source)),
                metadata_cache={},player_status=SimpleNamespace(remember=Mock()),series_preferences=SeriesPreferences(directory))
            payload=dict(kind='video',a=[dict(n='0',l='ja',t='')],s=[],name='Episode')
            from psp_streamer.server import file_identity
            server.metadata_cache[('token',file_identity(source))]=payload
            handler=object.__new__(AppHandler);handler.server=server;handler.headers={}
            self.assertEqual(handler.metadata('token')['series_saved'],0)
            server.series_preferences.save(server,'token',payload,dict(audio=0,subtitle=-1))
            result=handler.metadata('token')
            self.assertEqual(result['series_saved'],1)
            self.assertNotIn('series_saved',payload)

    def test_request_handler_without_socket(self):
        with tempfile.TemporaryDirectory() as directory:
            queue=Playlist(directory)
            server=SimpleNamespace(password='',playlist=queue,playlist_item=dict,
                player_status=SimpleNamespace(snapshot=lambda:dict(online=True,state='playing',id='current')))
            handler=object.__new__(AppHandler);handler.server=server;handler.path='/api/playlist'
            body=json.dumps(dict(action='play_next',revision=0,items=[dict(id='next')])).encode()
            handler.headers=Message();handler.headers['Content-Length']=str(len(body));handler.headers['Content-Type']='application/json'
            handler.rfile=io.BytesIO(body)
            handler.send_json=Mock();handler.authorized=Mock(return_value=True)
            handler.web_csrf=''
            # Exercise the real POST parser/auth route without opening a socket.
            handler.do_POST()
            self.assertNotIn('error',handler.send_json.call_args.args[0])
            self.assertEqual(queue.next('current')['id'],'next')

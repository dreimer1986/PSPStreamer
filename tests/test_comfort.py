"""Focused native checks for persistent conveniences, not decoder emulation."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class ComfortTests(unittest.TestCase):
    def test_resume_choices_provider_authority_and_local_position(self):
        ui=(ROOT/'psp-client/comfort_ui.h').read_text()
        start=ui.index('static int comfort_resume_prompt(')
        function=ui[start:ui.index('static int comfort_shortcuts(',start)]
        code=r'''
#include <assert.h>
#include "comfort_store.h"
#include "language.h"
static ComfortStore comfort_store;
static int provider_resume_seconds=-1,stream_start_seconds;
static float current_duration_seconds=1000;
static char current_media_name[]="Episode";
static void comfort_scope(char *s,int local){strcpy(s,local?"local":"server");}
enum {PSP_CTRL_CIRCLE=1,PSP_CTRL_CROSS=2,PSP_CTRL_SQUARE=4};
typedef struct {unsigned int Buttons;} SceCtrlData;
static unsigned int keys[3];static int at;
static void sceCtrlReadBufferPositive(SceCtrlData *p,int n){assert(n==1&&at<2);p->Buttons=keys[at++];}
static void sceKernelDelayThread(int n){(void)n;}
static void keep_awake(void){}
static void settings_shell(const char *s){(void)s;}
static void settings_help(const char *s){(void)s;}
static void settings_line(int r,int selected,const char *s){(void)r;(void)selected;(void)s;}
const char *tr(TextId id){return id==TXT_RESUME_QUESTION?"%d:%02d":"label";}
''' + function + r'''
int main(void){
    int i=comfort_find(&comfort_store,"server","file",1);comfort_store.records[i].seconds=123;
    keys[1]=PSP_CTRL_CROSS;assert(comfort_resume_prompt("file",0));assert(stream_start_seconds==123);
    at=0;keys[1]=PSP_CTRL_SQUARE;assert(comfort_resume_prompt("file",0));assert(!stream_start_seconds);
    at=0;keys[1]=PSP_CTRL_CIRCLE;assert(!comfort_resume_prompt("file",0));
    at=0;provider_resume_seconds=0;assert(comfort_resume_prompt("file",0));assert(!at);
    provider_resume_seconds=321;keys[1]=PSP_CTRL_CROSS;
    assert(comfort_resume_prompt("file",0));assert(stream_start_seconds==321);
    i=comfort_find(&comfort_store,"local","file",1);comfort_store.records[i].seconds=200;
    at=0;assert(comfort_resume_prompt("file",1));assert(stream_start_seconds==200);
    at=0;provider_resume_seconds=999;assert(comfort_resume_prompt("file",0));assert(!at);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            binary=Path(tmp)/'resume'
            subprocess.run(['cc','-x','c','-','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function',
                '-fsanitize=undefined','-I',str(ROOT/'psp-client'),'-o',str(binary)],input=code,text=True,check=True)
            subprocess.run([str(binary)],check=True,timeout=5)

    def test_download_inventory_and_remaining_space(self):
        client=(ROOT/'psp-client/offline_ui.h').read_text()
        main=(ROOT/'psp-client/main.c').read_text()
        def function(text,start):
            at=text.index(start);return text[at:text.index('\n}',at)+2]+'\n'
        code=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int SceUID;
typedef long long SceOff;
typedef struct {SceOff st_size;} SceIoStat;
#define PSP_SEEK_END 2
static int mode;
static unsigned long long offline_size(const char *p){return strstr(p,"movie.flv.part")?40:0;}
static int sceIoGetstat(const char *p,SceIoStat *s){
    if(strstr(p,"movie.flv") && mode==1){s->st_size=100;return 0;}
    if(strstr(p,"subtitles.ovl") && mode!=3){s->st_size=10;return 0;}
    if(strstr(p,"seek.idx")){s->st_size=0;return 0;}
    return -1;
}
static SceUID offline_open_movie(char *p,size_t cap,unsigned long long expected){(void)p;(void)cap;assert(expected==100);return mode==2?5:-1;}
static SceOff sceIoLseek(SceUID fd,int off,int whence){assert(fd==5&&!off&&whence==2);return 100;}
static void sceIoClose(SceUID fd){assert(fd==5);}
'''
        code+=function(main,'static int json_value(const char *from, const char *key, char *destination, size_t length) {')
        for signature in ['static int offline_leaf_valid(', 'static char *offline_json_object_end(', 'static unsigned long long offline_remaining_bytes(']:
            code+=function(client,signature)
        code+=r'''
int main(void){
    char meta[]="{\"files\":[{\"name\":\"movie.flv\",\"size\":100},{\"name\":\"subtitles.ovl\",\"size\":10},{\"name\":\"seek.idx\",\"size\":0}]}";
    assert(offline_remaining_bytes(meta,"job",1)==60);
    assert(offline_remaining_bytes(meta,"job",0)==1);
    mode=1;assert(offline_remaining_bytes(meta,"job",0)==0);
    assert(offline_remaining_bytes(meta,"job",1)==0);
    mode=2;assert(offline_remaining_bytes(meta,"job",0)==0); /* FAT alias */
    mode=3;assert(offline_remaining_bytes(meta,"job",1)==70);
    char invalid[]="{\"files\":[{\"name\":\"../bad\",\"size\":1}]}";
    assert(offline_remaining_bytes(invalid,"job",1)==~0ULL);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            binary=Path(tmp)/'inventory'
            subprocess.run(['cc','-x','c','-','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined',
                            '-o',str(binary)],input=code,text=True,check=True)
            subprocess.run([str(binary)],check=True,timeout=5)

    def test_storage_update_recovery_scopes_bookmarks_and_limits(self):
        main=(ROOT/'psp-client/main.c').read_text()
        functions=main[main.index('static void comfort_scope('):main.index('static const char *audio_quality_name(')]
        source=r'''
#include <assert.h>
#include "comfort_store.h"
#define COMFORT_PATH "state"
static ComfortStore comfort_store;
static char server_host[64]="home.test",status[128];
static int server_port=8091,server_https,comfort_timer_stopped,comfort_remaining;
static int playback_position_ms,stream_start_seconds,playback_reached_end,resume_pending,seek_requested;
static unsigned long long comfort_deadline,now;
static unsigned long long sceKernelGetSystemTimeWide(void){return now;}
enum {TXT_SETTINGS_FAILED};
static const char *tr(int id){(void)id;return "Save failed";}
''' + functions + r'''
int main(void) {
    ComfortStore restored;
    comfort_load_file(COMFORT_PATH,&comfort_store);
    char scope[80];comfort_scope(scope,0);
    assert(!strcmp(scope,"home.test:8091:0"));
    assert(comfort_find(&comfort_store,scope,"episode",0)<0);
    playback_position_ms=123000;
    comfort_finished("episode","Episode ä",0,0,100);
    comfort_load_file(COMFORT_PATH,&restored);
    int i=comfort_find(&restored,scope,"episode",0);
    assert(i>=0 && restored.records[i].seconds==123);
    assert(!strcmp(restored.records[i].name,"Episode ä"));
    assert(comfort_find(&restored,"different:8091:0","episode",0)<0);
    /* A failed startup does not erase the saved bookmark. */
    playback_position_ms=stream_start_seconds=0;
    comfort_finished("episode","Episode",0,0,-1);
    assert(comfort_store.records[i].seconds==123);
    comfort_store.records[i].favorite=1;
    assert(comfort_save_file(COMFORT_PATH,&comfort_store));
    playback_position_ms=456000;comfort_remaining=2;
    playback_reached_end=1;
    comfort_finished("episode","Episode",0,0,10);
    assert(comfort_remaining==1 && !comfort_timer_stopped && comfort_store.records[i].seconds==0);
    comfort_finished("next","Next",0,0,10);
    assert(comfort_remaining==0 && comfort_timer_stopped && !playback_reached_end);
    comfort_timer_stopped=0;now=100;comfort_deadline=101;
    assert(!comfort_expired());now=101;assert(comfort_expired());assert(!comfort_deadline);
    assert(!comfort_expired());
    /* Primary corruption recovers the previous complete generation. */
    FILE *f=fopen(COMFORT_PATH,"wb");assert(f);fputs("truncated",f);fclose(f);
    comfort_load_file(COMFORT_PATH,&restored);
    assert(restored.records[i].favorite && restored.records[i].seconds==0);
    /* A full favorites list must never evict a favorite. */
    for(int n=0;n<COMFORT_RECORDS;n++){
        snprintf(restored.records[n].id,512,"favorite-%d",n);restored.records[n].favorite=1;
    }
    assert(comfort_find(&restored,scope,"new",1)<0);
    restored.records[3].favorite=0;
    assert(comfort_find(&restored,scope,"new",1)==3);
    comfort_scope(scope,1);assert(!strcmp(scope,"local"));
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            binary=Path(tmp)/'comfort'
            subprocess.run(['cc','-x','c','-','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined',
                '-I',str(ROOT/'psp-client'),'-o',str(binary)],input=source,text=True,check=True)
            subprocess.run([str(binary)],cwd=tmp,check=True,timeout=5)

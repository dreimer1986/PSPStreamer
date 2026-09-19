import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class OfflineOutputTests(unittest.TestCase):
    def test_local_music_final_block_drain_cancel_and_error(self):
        source=(ROOT/'psp-client/main.c').read_text()
        worker=source[source.index('static int audio_thread(SceSize'):source.index('#include "music_ui.h"')]
        cleanup=worker[worker.index('cleanup:\n')+len('cleanup:\n'):]
        harness=r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#define AUDIO_BLOCK_SAMPLES 4608
#define AUDIO_QUEUE_BLOCKS 8
#define MP3_DECODE_SAMPLES 1152
static short audio_samples[AUDIO_BLOCK_SAMPLES*2*AUDIO_QUEUE_BLOCKS];
static unsigned int audio_block_timestamp_ms[AUDIO_QUEUE_BLOCKS];
static int audio_dac_samples=AUDIO_BLOCK_SAMPLES,audio_queue_write;
static int audio_queue_ready_sema=2,audio_queue_free_sema=3;
static int timed_active,timed_running,timed_audio_done,audio_state,audio_running,audio_start;
static int offline_music=1,offline_music_eof,audio_queue_primed,audio_played_blocks,audio_blocks_published;
static int audio_socket_fd=-1,closed,published,released,delays;
static void *mp3_codec_work;
static void sceKernelDcacheInvalidateRange(void *p,int n){(void)p;(void)n;}
static void sceKernelDcacheWritebackRange(void *p,int n){(void)p;(void)n;}
static void sceKernelSignalSema(int s,int n){assert(n==1);if(s==2)published++;else {assert(s==3);released++;}}
static void sceKernelDelayThread(int n){assert(n==10000 && audio_queue_primed);delays++;audio_played_blocks=audio_blocks_published;}
static void sceIoClose(int fd){assert(fd==7);closed++;}
static void connection_close(int fd){(void)fd;assert(0);}
static int finish(int frames_in_block) {
    struct {void *data;} timed_packet={0};
    int write_slot_reserved=1,block_bytes=AUDIO_BLOCK_SAMPLES*4;
    unsigned int block_pts=0;
    int local_fd=7,socket_fd=-1;
''' + cleanup + r'''
int main(void) {
    for(int test=0;test<3;test++) {
        audio_queue_write=audio_blocks_published=audio_played_blocks=0;
        audio_queue_primed=closed=published=released=delays=0;
        audio_state=test==2?-24:10;
        audio_running=test!=1;offline_music_eof=test!=1;
        for(unsigned i=0;i<sizeof(audio_samples)/sizeof(short);i++)audio_samples[i]=17;
        assert(finish(2)==0);
        assert(closed==1 && !audio_running);
        if(test==0) {
            assert(published==1 && delays==1 && audio_state==15);
            assert(audio_played_blocks==1 && !released);
            for(int i=0;i<4608;i++)assert(audio_samples[i]==17);
            for(int i=4608;i<9216;i++)assert(audio_samples[i]==0);
        } else assert(!published && !delays && released==1);
    }
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'music-eof'
            subprocess.run(['cc','-x','c','-','-Wall','-Wextra','-Werror','-fsanitize=undefined',
                            '-o',str(binary)],input=harness,text=True,check=True)
            subprocess.run([str(binary)],check=True,timeout=3)

    def test_seek_preroll_reservoir_bounds_fragmentation_and_bad_index(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / 'preroll'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            '-fsanitize=undefined', '-I', str(ROOT / 'psp-client'),
                            str(ROOT / 'tests/offline_preroll_harness.c'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=3)

    def test_pc_copied_unicode_names_resolve_through_fat_alias(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / 'filename'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                            '-I', str(ROOT / 'psp-client'), str(ROOT / 'tests/offline_filename_harness.c'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=3)

    def test_profile_guard_distinguishes_output_from_decoder_errors(self):
        source = (ROOT / 'psp-client/main.c').read_text()
        start = source.index('    tvout_video_active = tvout_begin_video() == 0;')
        end = source.index('    if (tvout_video_active) memset', start)
        guard = source[start:end]
        harness = '''
#include <assert.h>
#include <stdint.h>
static int offline_active, offline_profile_tv, tvout_video_active, cable, restored;
static const char *video_step;
#define TXT_DOWNLOAD_PROFILE 1
static const char *tr(int id) {(void)id;return "profile mismatch";}
static int tvout_begin_video(void) {return cable?0:-1;}
static void tvout_end_video(void) {restored++;}
static int check(void) {
''' + guard + '''
return 0;
}
int main(void) {
    offline_active=1;
    for(cable=0;cable<=1;cable++)for(offline_profile_tv=0;offline_profile_tv<=1;offline_profile_tv++) {
        restored=0;video_step=0;
        int result=check();
        if(cable==offline_profile_tv) assert(!result && !restored);
        else {
            assert((uint32_t)result==0xFFFFFA87u && video_step);
            assert(!tvout_video_active && restored==cable);
        }
    }
    offline_active=0;
    for(cable=0;cable<=1;cable++)assert(!check());
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / 'profile'
            subprocess.run(['cc', '-x', 'c', '-', '-Wall', '-Wextra', '-Werror', '-o', str(binary)],
                           input=harness, text=True, check=True)
            subprocess.run([str(binary)], check=True, timeout=3)

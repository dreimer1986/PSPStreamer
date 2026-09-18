from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


class RadioClientTests(unittest.TestCase):
    def test_retry_pause_resume_stop_and_remote_replacement(self):
        source=(ROOT/'psp-client/main.c').read_text()
        wrapper=source[source.index('static int play_audio('):source.index('static int play_h264(')]
        harness=(ROOT/'tests/radio_playback_harness.c').read_text().replace('/* RADIO_WRAPPER */',wrapper)
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'radio.c'
            binary=Path(directory)/'radio'
            path.write_text(harness)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined',str(path),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=5)

    def test_caption_is_incremental_and_preserves_utf8(self):
        harness=r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
typedef unsigned int u32;
static struct {unsigned int full_frames,incremental_frames;} tv_music={1,1},lcd_music={1,1};
static int tv_ui_active,tvout_video_active,tv_music_title_bottom;
static struct {int tv;} display_output;
static struct {u32 *pixels;} tv_canvas;
enum {TV_GUI_STRIDE=768,TV_WHITE=0xffffff,TV_CYAN=0xffff00};
static int draws,restores;
static char last[192];
static void sceDisplayWaitVblankStart(void){}
static void tv_restore_rect(int x,int y,int w,int h){assert(x==34 && y==65 && w==498 && h==40);}
static int tv_text(int x,int y,int cols,int lines,u32 color,const char *format,...){
 (void)x;(void)y;(void)cols;(void)lines;(void)color;(void)format;return 100;
}
static void lcd_music_restore(int x,int y,int w,int h,int full){
 assert(x==38 && y==40 && w==310 && h==20 && !full);restores++;
}
static void gui_text(int x,int y,u32 color,const char *format,...){
 (void)x;(void)y;(void)color;va_list ap;va_start(ap,format);
 vsnprintf(last,sizeof(last),format,ap);va_end(ap);draws++;
}
#include "music_caption.h"
int main(void){
 music_caption("Station","Song",0);assert(draws==2 && restores==1);
 for(int i=0;i<100;i++)music_caption("Station","Song",0);
 assert(draws==2 && restores==1);
 music_caption("Station","New song",0);assert(draws==4 && restores==2);
 music_caption("Station","No overlay",1);assert(draws==4);
 char unicode[180],out[160];
 for(int i=0;i<50;i++){unicode[i*2]=(char)0xc3;unicode[i*2+1]=(char)0xb6;}unicode[100]=0;
 music_caption_line(out,unicode);assert(strlen(out)==78 && (unsigned char)out[77]==0xb6);
 music_caption("Station",unicode,0);assert(strlen(last)==78);
 lcd_music.incremental_frames=0;music_caption("Station",unicode,0);assert(draws==8);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'caption.c';binary=Path(directory)/'caption'
            path.write_text(harness)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined',
                            '-I',str(ROOT/'psp-client'),str(path),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=5)

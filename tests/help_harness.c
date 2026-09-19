#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/mman.h>
#include "language.h"
typedef uint32_t u32;
typedef struct {unsigned int Buttons;} SceCtrlData;
enum {PSP_CTRL_UP=1,PSP_CTRL_DOWN=2,PSP_CTRL_LEFT=4,PSP_CTRL_RIGHT=8,
    PSP_CTRL_CROSS=16,PSP_CTRL_CIRCLE=32,PSP_CTRL_START=64,PSP_CTRL_SELECT=128,
    PSP_CTRL_LTRIGGER=256,PSP_CTRL_RTRIGGER=512,PSP_CTRL_TRIANGLE=1024,PSP_CTRL_SQUARE=2048};
#define VIDEO_WIDTH 480
#define VIDEO_HEIGHT 272
#define VIDEO_STRIDE 512
#define SUBTITLE_FONT_CELL_WIDTH 16
#define SUBTITLE_FONT_CELL_HEIGHT 20
#include "tv_canvas.h"
#include "help_pages.h"
static int tv_ui_active;
static TvCanvas tv_canvas;
static unsigned char font[81920],*subtitle_font=font;
static void subtitle_load_font(void) {}
static int subtitle_utf8_char(const char **text) {
    const unsigned char *s=(const unsigned char *)*text;
    if(s[0]==0xc3 && s[1]){*text+=2;return 0xc0+(s[1]&63);}
    if(s[0]==0xc2 && s[1]){*text+=2;return s[1];}
    (*text)++;return s[0]<128?s[0]:'?';
}
static int tv_utf8_char(const char **text) {return subtitle_utf8_char(text);}
/* PRODUCTION_DRAWING */
static void tv_present(void) {}
static unsigned long long tick;
static unsigned int input[]={PSP_CTRL_CROSS,0,PSP_CTRL_RTRIGGER,0,PSP_CTRL_DOWN,0,PSP_CTRL_CIRCLE};
static int key_index;
static void sceCtrlReadBufferPositive(SceCtrlData *pad,int n) {
    assert(n==1 && key_index<(int)(sizeof(input)/sizeof(input[0])));
    pad->Buttons=input[key_index++];
}
static void keep_awake(void) {}
static unsigned long long sceKernelGetSystemTimeWide(void) {return tick;}
static void sceKernelDelayThread(int us) {tick+=us;}
#include "help_ui.h"
static void check_text(const char *text) {
    assert(text && *text);
    const char *p=text;int lcd=0,tv=0;
    while(*p) {
        int glyph=subtitle_utf8_char(&p),advance=6;
        const unsigned char *bitmap=font+(glyph>>4)*20*256+(glyph&15)*16;
        for(int y=0;y<16;y++)for(int x=0;x<12;x++)
            if(tv_glyph_alpha(bitmap,x,y) && advance<x+2)advance=x+2;
        lcd+=7;tv+=advance;
    }
    if(lcd>280 || tv>430)fprintf(stderr,"Help overflow: %s (%d LCD / %d TV)\n",text,lcd,tv);
    assert(lcd<=280 && tv<=430);
}
static void preview(const char *directory,int page) {
    if(!directory)return;
    char path[1024];snprintf(path,sizeof(path),"%s/help-%s-%s-%02d.ppm",directory,
        language_code(),tv_ui_active?"tv":"lcd",page);
    FILE *f=fopen(path,"wb");assert(f);
    int w=tv_ui_active?720:480,h=tv_ui_active?480:272,stride=tv_ui_active?768:512;
    const u32 *pixels=tv_ui_active?tv_canvas.pixels:(u32 *)0x44000000;
    fprintf(f,"P6\n%d %d\n255\n",w,h);
    for(int y=0;y<h;y++)for(int x=0;x<w;x++) {
        u32 c=pixels[y*stride+x];unsigned char rgb[]={c&255,(c>>8)&255,(c>>16)&255};
        fwrite(rgb,1,3,f);
    }
    fclose(f);
}
int main(void) {
    FILE *f=fopen("assets/subtitle_font.raw","rb");assert(f);
    assert(fread(font,1,sizeof(font),f)==sizeof(font));fclose(f);
    assert(mmap((void *)0x44000000,512*272*4,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==(void *)0x44000000);
    tv_canvas.pixels=calloc(768*480,4);assert(tv_canvas.pixels);
    assert(help_first_page(-1)==0);
    for(int page=0;page<HELP_PAGE_COUNT;page++) {
        assert(help_pages[page].buttons);
        assert(help_move(help_move(page,0,1),0,-1)==page);
        assert(help_pages[help_move(page,1,1)].topic==(help_pages[page].topic+1)%HELP_TOPICS);
        assert(help_pages[help_move(page,1,-1)].topic==(help_pages[page].topic+HELP_TOPICS-1)%HELP_TOPICS);
    }
    for(int language=0;language<2;language++) {
        language_set_code(language?"de":"en");
        const HelpText *texts=language?help_de:help_en;
        for(int p=0;p<HELP_PAGE_COUNT;p++) {
            const HelpText *t=texts+p;
            check_text(t->title);check_text(t->step1_title);check_text(t->step1_text);
            check_text(t->step2_title);check_text(t->step2_text);
            check_text(t->step3_title);check_text(t->step3_text);
            for(tv_ui_active=0;tv_ui_active<2;tv_ui_active++) {
                help_draw(p);preview(getenv("HELP_PREVIEW_DIR"),p);
            }
        }
    }
    tv_ui_active=0;help_open(HELP_BROWSE);assert(key_index==7);
    free(tv_canvas.pixels);
    return 0;
}

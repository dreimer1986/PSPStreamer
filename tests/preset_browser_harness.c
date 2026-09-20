#include <assert.h>
#include "preset_catalog.h"
#include "milkdrop_preset.h"
#include "language.h"
typedef struct { unsigned int Buttons; } SceCtrlData;
enum { PSP_CTRL_CIRCLE=1,PSP_CTRL_CROSS=2,PSP_CTRL_UP=4,PSP_CTRL_DOWN=8,
       PSP_CTRL_LTRIGGER=16,PSP_CTRL_RTRIGGER=32,PSP_CTRL_START=64,PSP_CTRL_SQUARE=128,PSP_CTRL_TRIANGLE=256 };
static int music_preset_auto,music_preset_seconds=60;
enum { MUSIC_REMOTE_NONE,MUSIC_REMOTE_PAUSE,MUSIC_REMOTE_RESUME,MUSIC_REMOTE_STOP };
static int audio_running=1,audio_start=1,music_remote_action,tv_ui_active;
static char music_preset_file[256]="Alpha.milk";
static unsigned int input[32],cursor;
static unsigned long long tick;
static int expected_stop;
static void keep_awake(void) {}
static void video_watch_ping(const char *s) { (void)s; }
static void sceCtrlPeekBufferPositive(SceCtrlData *p,int n) {
    assert(n==1 && cursor<32); p->Buttons=input[cursor++];
    if(expected_stop && cursor==1) music_remote_action=MUSIC_REMOTE_STOP;
}
static unsigned long long sceKernelGetSystemTimeWide(void) { return tick; }
static void sceKernelDelayThread(int us) { tick+=us; }
#define tv_shell(...) ((void)0)
#define tv_text(...) ((void)0)
#define tv_help(...) ((void)0)
#define tv_present(...) ((void)0)
#define gui_library_shell(...) ((void)0)
#define gui_text(...) ((void)0)
MdFilePreset md_custom_preset;
int md_load_preset(const char *path,MdFilePreset *p,MdFileError *e) {
    (void)p;(void)e; assert(!strcmp(path,"presets/Zulu.milk")); return MD_FILE_OK;
}
#include "preset_browser.h"
int main(void) {
    for(int tv=0;tv<2;tv++) {
        tv_ui_active=tv; cursor=0; tick=0; strcpy(music_preset_file,"Alpha.milk");
        input[0]=0; input[1]=PSP_CTRL_DOWN; input[2]=0; input[3]=PSP_CTRL_CROSS;
        assert(music_choose_preset()==1); assert(!strcmp(music_preset_file,"Zulu.milk"));
        MdFileError e; assert(music_load_selected(&e)==MD_FILE_OK);
        cursor=0; input[0]=0; input[1]=PSP_CTRL_CIRCLE;
        assert(music_choose_preset()==0); assert(!strcmp(music_preset_file,"Zulu.milk"));
        cursor=0;input[0]=0;input[1]=PSP_CTRL_SQUARE;input[2]=0;input[3]=PSP_CTRL_TRIANGLE;input[4]=0;input[5]=PSP_CTRL_CIRCLE;
        int before=music_preset_auto;
        assert(music_choose_preset()==0);assert(music_preset_auto==(before+1)%4);
    }
    cursor=0; expected_stop=1; input[0]=0;
    assert(music_choose_preset()==0); assert(cursor==1);
    /* Settings ignores stale music state and debounces the opening X. */
    assert(!mkdir("presets/Geiss",0700));
    FILE *f=fopen("presets/Geiss/Test.milk","wb");assert(f);fclose(f);
    char choice[256]="Alpha.milk";
    expected_stop=0;audio_running=0;music_remote_action=MUSIC_REMOTE_STOP;
    unsigned int enter[]={PSP_CTRL_CROSS,0,PSP_CTRL_DOWN,0,PSP_CTRL_CROSS,0,PSP_CTRL_DOWN,0,PSP_CTRL_CROSS};
    memcpy(input,enter,sizeof(enter));cursor=0;
    assert(preset_choose(choice,0)==1 && !strcmp(choice,"Geiss/Test.milk"));
    assert(cursor==9 && music_remote_action==MUSIC_REMOTE_STOP);
    unsigned int parent[]={0,PSP_CTRL_UP,0,PSP_CTRL_CROSS,0,PSP_CTRL_CROSS};
    memcpy(input,parent,sizeof(parent));cursor=0;
    assert(preset_choose(choice,0)==1 && !strcmp(choice,"Alpha.milk"));
    PresetCatalog catalog;
    assert(!preset_catalog_browse(&catalog,"presets/absent",1));
    assert(catalog.count==1 && !strcmp(catalog.names[0],".."));
    assert(preset_path_valid("Geiss/Test.milk"));
    assert(!preset_path_valid("../Test.milk") && !preset_path_valid("Geiss/../Test.milk"));
    assert(!preset_path_valid("/Test.milk") && !preset_path_valid("ms0:/Test.milk"));
    assert(!preset_path_valid("Geiss//Test.milk"));
    return 0;
}

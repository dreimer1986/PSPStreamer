#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "language.h"
typedef struct {unsigned int Buttons;} SceCtrlData;
enum {PSP_CTRL_UP=1,PSP_CTRL_DOWN=2,PSP_CTRL_LEFT=4,PSP_CTRL_RIGHT=8,
      PSP_CTRL_CROSS=16,PSP_CTRL_CIRCLE=32,PSP_CTRL_START=64,PSP_CTRL_SELECT=128,
      PSP_CTRL_LTRIGGER=256,PSP_CTRL_RTRIGGER=512};
#define TV_AMBER 1
#define TV_WHITE 2
static char server_host[64]="example.test",server_password[129]="ä:test",music_preset_file[256]="active.milk";
static char current_path[512]="folder",remote_session[40]="session",status[256],language[3]="en";
static int server_port=8091,server_https,tv_ui_auto,selected_audio_track,selected_subtitle_track=-1;
static int selected_audio_quality=2,selected_video_fps,playback_volume=24,audio_shuffle;
static int music_preset_auto,music_preset_seconds=60,music_preset_fade_ms=1500;
static int debug_enabled;
static int music_cpu_mhz,milkdrop_cpu_mhz,video_cpu_mhz,idle_cpu_mhz,screen_idle;
static int playback_clock_valid(int mhz){return mhz==0||(mhz>=66&&mhz<=471);}
static int clock_choice(int mhz,int direction){(void)mhz;return direction>0?66:471;}
static int clock_error;
static int clock_control_status(void){return 0;}
static int have_cached_server_address=1,resume_pending=1,remote_control_sequence=3,item_count=4,tv_ui_active;
static unsigned int keys[64];static int position,total,save_failed,saved;
static unsigned long long tick;
const char *tr(TextId id) {(void)id;return "label";}
const char *language_code(void) {return language;}
void language_set_code(const char *s) {strcpy(language,s);}
static void server_auth_update(void) {}
static int save_playback_settings(void) {saved++;return save_failed?-1:0;}
static void tv_shell(const char *s) {(void)s;}
static void gui_library_shell(const char *s) {(void)s;}
static void tv_text(int x,int y,int w,int h,int c,const char *f,...) {(void)x;(void)y;(void)w;(void)h;(void)c;(void)f;}
static void gui_text(int x,int y,int c,const char *f,...) {(void)x;(void)y;(void)c;(void)f;}
static void tv_help(const char *s) {(void)s;}
static void tv_present(void) {}
static void keep_awake(void) {}
static int preset_visits;
static int preset_choose(char selection[256],int music) {
    assert(!music);preset_visits++;strcpy(selection,"Geiss/Hyperdrive.milk");return 1;
}
static void sceCtrlReadBufferPositive(SceCtrlData *pad,int n) {assert(n==1&&position<total);pad->Buttons=keys[position++];}
static unsigned long long sceKernelGetSystemTimeWide(void) {return tick;}
static void sceKernelDelayThread(int us) {tick+=us;}
#define HELP_BROWSE 0
static int help_visits;
static int output_failed,output_changes;
static int tv_menu_select(int tv) {output_changes++;if(output_failed)return -1;tv_ui_active=tv;return 0;}
static void help_open(int topic) {assert(topic==HELP_BROWSE);help_visits++;}
#include "app_settings.h"
static void sequence(const unsigned int *values,int count) {memcpy(keys,values,count*sizeof(*values));total=count;position=0;tick=0;}
int main(void) {
    AppSettings state;settings_capture(&state);assert(!strcmp(state.password,"ä:test"));
    assert(state.value[SET_DEBUG]==0);
    state.value[SET_CPU_MUSIC]=222;state.value[SET_CPU_MILKDROP]=443;
    state.value[SET_CPU_VIDEO]=333;state.value[SET_CPU_IDLE]=111;state.value[SET_SCREEN]=1;
    settings_apply(&state);
    assert(music_cpu_mhz==222&&milkdrop_cpu_mhz==443&&video_cpu_mhz==333&&idle_cpu_mhz==111&&screen_idle==1);
    settings_capture(&state);assert(state.value[SET_CPU_IDLE]==111&&state.value[SET_CPU_MILKDROP]==443);
    state.value[SET_DEBUG]=1;settings_apply(&state);assert(debug_enabled==1);
    state.value[SET_DEBUG]=0;settings_apply(&state);assert(debug_enabled==0);
    memset(music_preset_file,'x',250);music_preset_file[250]=0;
    settings_capture(&state);assert(strlen(state.preset)==250);settings_apply(&state);
    const unsigned int help[]={0,PSP_CTRL_CROSS,PSP_CTRL_CIRCLE,0,PSP_CTRL_CIRCLE};
    sequence(help,5);assert(app_settings()==0&&help_visits==1&&saved==0);
    const unsigned int output[]={0,PSP_CTRL_DOWN,PSP_CTRL_CROSS,PSP_CTRL_CROSS,PSP_CTRL_CIRCLE};
    sequence(output,5);assert(app_settings()==0&&tv_ui_active==1&&output_changes==1&&saved==0&&tv_ui_auto==0);
    sequence(output,5);assert(app_settings()==0&&tv_ui_active==0&&output_changes==2&&saved==0);
    output_failed=1;sequence(output,5);assert(app_settings()==0&&tv_ui_active==0&&output_changes==3);output_failed=0;
    const unsigned int cancel[]={0,PSP_CTRL_DOWN,0,PSP_CTRL_DOWN,0,PSP_CTRL_DOWN,PSP_CTRL_RIGHT,PSP_CTRL_CIRCLE};
    sequence(cancel,8);assert(app_settings()==0&&server_port==8091&&saved==0);
    const unsigned int apply[]={0,PSP_CTRL_DOWN,0,PSP_CTRL_DOWN,0,PSP_CTRL_DOWN,PSP_CTRL_RIGHT,PSP_CTRL_START};
    sequence(apply,8);assert(app_settings()==1&&server_port==8092&&saved==1);
    assert(!have_cached_server_address&&!resume_pending&&!remote_session[0]&&!current_path[0]);
    save_failed=1;sequence(apply,8);assert(app_settings()==-1&&server_port==8092);
    char text[256]="aä";
    const unsigned int backspace[]={0,PSP_CTRL_LTRIGGER,PSP_CTRL_START};
    sequence(backspace,3);assert(settings_text(text,sizeof(text),1,"text")==1&&!strcmp(text,"a"));
    const unsigned int discard[]={0,PSP_CTRL_RTRIGGER,PSP_CTRL_CIRCLE};
    sequence(discard,3);assert(settings_text(text,sizeof(text),0,"text")==0&&!strcmp(text,"a"));
    /* 240-ms taps used to repeat after 150 ms. Cover both axes and directions,
     * then verify that a genuine hold still repeats after the initial delay. */
    save_failed=0;
    for(int tv=0;tv<2;tv++) for(int dir=0;dir<2;dir++) {
        unsigned int tap[64];int n=0;
        tv_ui_active=tv;server_port=8091;
        tap[n++]=0;
        for(int i=0;i<12;i++)tap[n++]=PSP_CTRL_DOWN; /* output row */
        tap[n++]=0;tap[n++]=PSP_CTRL_DOWN; /* host */
        tap[n++]=0;tap[n++]=PSP_CTRL_DOWN; /* port */
        tap[n++]=0;tap[n++]=PSP_CTRL_DOWN; /* password */
        tap[n++]=0;
        for(int i=0;i<12;i++)tap[n++]=PSP_CTRL_UP; /* port */
        tap[n++]=0;
        for(int i=0;i<12;i++)tap[n++]=dir?PSP_CTRL_RIGHT:PSP_CTRL_LEFT;
        tap[n++]=PSP_CTRL_START;
        sequence(tap,n);assert(app_settings()==1&&server_port==(dir?8092:8090));
    }
    unsigned int held[64];int n=0;server_port=8091;
    held[n++]=0;held[n++]=PSP_CTRL_DOWN;held[n++]=0;
    held[n++]=PSP_CTRL_DOWN;held[n++]=0;held[n++]=PSP_CTRL_DOWN;held[n++]=0;
    for(int i=0;i<30;i++)held[n++]=PSP_CTRL_RIGHT; /* 0, 400 and 560 ms */
    held[n++]=PSP_CTRL_START;
    sequence(held,n);assert(app_settings()==1&&server_port==8094);
    n=0;held[n++]=0;
    for(int i=0;i<12;i++)held[n++]=PSP_CTRL_RIGHT;
    held[n++]=PSP_CTRL_CROSS;held[n++]=PSP_CTRL_START;
    text[0]=0;sequence(held,n);
    assert(settings_text(text,sizeof(text),0,"text")==1&&!strcmp(text,"!"));
    /* Both submenus are reachable without audio running. OC cancel has no
     * filesystem side effects. A preset remains a draft until outer START. */
    const unsigned int oc_cancel[]={0,PSP_CTRL_UP,PSP_CTRL_CROSS,0,PSP_CTRL_CIRCLE,0,PSP_CTRL_CIRCLE};
    sequence(oc_cancel,7);assert(app_settings()==0);
    for(int accept=0;accept<2;accept++) {
        n=0;held[n++]=0;
        for(int i=0;i<SET_PRESET+2;i++){held[n++]=PSP_CTRL_DOWN;held[n++]=0;}
        held[n++]=PSP_CTRL_CROSS;held[n++]=0;
        held[n++]=accept?PSP_CTRL_START:PSP_CTRL_CIRCLE;
        strcpy(music_preset_file,"active.milk");sequence(held,n);
        assert(app_settings()==accept);
        assert(!strcmp(music_preset_file,accept?"Geiss/Hyperdrive.milk":"active.milk"));
    }
    assert(preset_visits==2);
    return 0;
}

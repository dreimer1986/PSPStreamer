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
static int preset_name_valid(const char *s) {return *s&&!strchr(s,'/');}
static void sceCtrlReadBufferPositive(SceCtrlData *pad,int n) {assert(n==1&&position<total);pad->Buttons=keys[position++];}
static unsigned long long sceKernelGetSystemTimeWide(void) {return tick;}
static void sceKernelDelayThread(int us) {tick+=us;}
#define HELP_BROWSE 0
static int help_visits;
static void help_open(int topic) {assert(topic==HELP_BROWSE);help_visits++;}
#include "app_settings.h"
static void sequence(const unsigned int *values,int count) {memcpy(keys,values,count*sizeof(*values));total=count;position=0;tick=0;}
int main(void) {
    AppSettings state;settings_capture(&state);assert(!strcmp(state.password,"ä:test"));
    assert(state.value[SET_DEBUG]==0);
    state.value[SET_DEBUG]=1;settings_apply(&state);assert(debug_enabled==1);
    state.value[SET_DEBUG]=0;settings_apply(&state);assert(debug_enabled==0);
    memset(music_preset_file,'x',250);music_preset_file[250]=0;
    settings_capture(&state);assert(strlen(state.preset)==250);settings_apply(&state);
    const unsigned int help[]={0,PSP_CTRL_CROSS,PSP_CTRL_CIRCLE,0,PSP_CTRL_CIRCLE};
    sequence(help,5);assert(app_settings()==0&&help_visits==1&&saved==0);
    const unsigned int cancel[]={0,PSP_CTRL_DOWN,0,PSP_CTRL_DOWN,PSP_CTRL_RIGHT,PSP_CTRL_CIRCLE};
    sequence(cancel,6);assert(app_settings()==0&&server_port==8091&&saved==0);
    const unsigned int apply[]={0,PSP_CTRL_DOWN,0,PSP_CTRL_DOWN,PSP_CTRL_RIGHT,PSP_CTRL_START};
    sequence(apply,6);assert(app_settings()==1&&server_port==8092&&saved==1);
    assert(!have_cached_server_address&&!resume_pending&&!remote_session[0]&&!current_path[0]);
    save_failed=1;sequence(apply,6);assert(app_settings()==-1&&server_port==8092);
    char text[256]="aä";
    const unsigned int backspace[]={0,PSP_CTRL_LTRIGGER,PSP_CTRL_START};
    sequence(backspace,3);assert(settings_text(text,sizeof(text),1,"text")==1&&!strcmp(text,"a"));
    const unsigned int discard[]={0,PSP_CTRL_RTRIGGER,PSP_CTRL_CIRCLE};
    sequence(discard,3);assert(settings_text(text,sizeof(text),0,"text")==0&&!strcmp(text,"a"));
    return 0;
}

#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef struct {unsigned int Buttons;} SceCtrlData;
enum {PSP_CTRL_START=1,PSP_CTRL_CIRCLE=2,PSP_CTRL_SELECT=4,PSP_CTRL_CROSS=8,PSP_CTRL_SQUARE=16};
enum {MUSIC_REMOTE_NONE,MUSIC_REMOTE_PAUSE,MUSIC_REMOTE_RESUME,MUSIC_REMOTE_STOP,MUSIC_REMOTE_SEEK,MUSIC_REMOTE_PLAY};
enum {TXT_RADIO_PAUSED,TXT_RADIO_RECONNECT,PSP_NET_APCTL_STATE_GOT_IP=4};
static int radio_next_action,tv_ui_active,music_remote_action,radio_connect_wait;
static int stream_start_seconds,resume_pending,seek_requested;
static int scenario,calls,starts,stops,polls;
static unsigned long long clock_us;
static const char *tr(int id){return id==TXT_RADIO_PAUSED?"Paused":"Reconnecting";}
static int radio_is_live(const char *id){return !strncmp(id,"radio.",6);}
static void keep_awake(void){}
static void lcd_music_reset(void){}
static void tv_music_reset(void){}
static void tv_draw_music(const char *title,int full){assert(title && !full);}
static void lcd_draw_music(const char *title,int full){assert(title && !full);}
static unsigned long long sceKernelGetSystemTimeWide(void){return clock_us;}
static void sceKernelDelayThread(int us){clock_us+=us;assert(clock_us<10000000);}
static int sceNetApctlGetState(int *state){*state=PSP_NET_APCTL_STATE_GOT_IP;return 0;}
static int wait_for_network_restore(void){return 0;}
static int music_remote_start(void){starts++;return 0;}
static void music_remote_stop(void){stops++;music_remote_action=0;}
static void sceCtrlPeekBufferPositive(SceCtrlData *pad,int n){
    assert(n==1);polls++;pad->Buttons=0;
    if(scenario==1 && polls==4)music_remote_action=MUSIC_REMOTE_RESUME;
    if(scenario==2 && polls==4)pad->Buttons=PSP_CTRL_START;
    if(scenario==3 && polls==4)music_remote_action=MUSIC_REMOTE_PLAY;
}
static int play_audio_once(const char *id,const char *title){
    assert(id && title);calls++;
    radio_next_action=calls==1?(scenario==1?2:1):0;
    return calls==1?-14:0;
}
/* RADIO_WRAPPER */
int main(void){
    for(scenario=0;scenario<4;scenario++){
        calls=starts=stops=polls=0;clock_us=0;music_remote_action=0;
        assert(play_audio("radio.test","Station")==0);
        assert(calls==(scenario<2?2:1));
        assert(starts==1 && stops==1);
    }
    calls=starts=stops=polls=0;clock_us=0;
    assert(play_audio("file-id","File")==-14);
    assert(calls==1 && starts==0 && stops==0);
    return 0;
}

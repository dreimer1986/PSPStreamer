#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#define SO_NONBLOCK 0x1009
#define SCE_NET_INET_POLLOUT 4
#define SCE_NET_INET_POLLIN 1
struct SceNetInetPollfd {int fd,events,revents;};
static unsigned long long tick;
static int server_https,server_port=80,mode,nonblock,sends;
static const char *server_host="example.test";
static volatile int running=1;
static unsigned long long sceKernelGetSystemTimeWide(void){return tick;}
static int sceNetInetSetsockopt(int fd,int level,int option,const void *v,int len){
    (void)fd;(void)level;(void)len;assert(option==SO_NONBLOCK);nonblock=*(const int *)v;return 0;
}
static int sceNetInetConnect(int fd,struct sockaddr *a,int n){(void)fd;(void)a;(void)n;return -1;}
static int sceNetInetPoll(struct SceNetInetPollfd *p,int n,int ms){
    (void)n;tick+=(unsigned int)ms*1000;
    if(mode==2)running=0;
    if(mode==1 || mode==2)return 0;
    p->revents=p->events;return 1;
}
static int sceNetInetGetsockopt(int fd,int l,int o,void *v,socklen_t *n){
    (void)fd;(void)l;(void)o;(void)n;*(int *)v=mode==3?1:0;return 0;
}
static int sceNetInetGetErrno(void){return 35;}
static int sceNetInetSend(int fd,const void *b,int n,int f){
    (void)fd;(void)b;(void)f;sends++;return mode==4?-1:(n>3?3:n);
}
static int sceNetInetRecv(int fd,void *b,int n,int f){(void)fd;(void)b;(void)n;(void)f;return mode==4?-1:0;}
static int tls_open(int fd,const char *h,int p,volatile int *r,int ms){
    (void)fd;(void)h;(void)p;assert(r==&running && ms==15000);return *r?0:-1;
}
static int tls_send(int fd,const void *b,int n,volatile int *r,int ms){
    (void)fd;(void)b;assert(r==&running && ms==15000);return *r?n:-1;
}
static int tls_recv(int fd,void *b,int n,int ms){(void)fd;(void)b;(void)n;tick+=ms*1000;return -2;}
#include "playback_transport.h"

typedef struct {unsigned int Buttons;} SceCtrlData;
#define PSP_CTRL_START 1
#define PSP_CTRL_CIRCLE 2
#define PSP_CTRL_CROSS 4
#define PSP_CTRL_SQUARE 8
#define MUSIC_REMOTE_NONE 0
#define MUSIC_REMOTE_PAUSE 1
#define MUSIC_REMOTE_RESUME 2
#define MUSIC_REMOTE_STOP 3
#define MUSIC_REMOTE_PLAY 4
#define MUSIC_REMOTE_SEEK 5
#define TXT_STREAM_RECONNECT 0
typedef int TextId;
#define TXT_STREAM_WIFI 1
#define TXT_STREAM_SERVER 2
#define TXT_STREAM_RESUME 3
#define PSP_NET_APCTL_STATE_GOT_IP 4
static int network_ready,http_ready,forced,inputs_stopped;
static int sceNetApctlGetState(int *state){*state=4;return 0;}
static void input_remote_stop(void){inputs_stopped++;}
static int wifi_associate(int force){assert(force);forced++;return 0;}
static int plex_paused,plex_started,tv_ui_active,music_remote_action,music_remote_seconds;
static int playback_position_ms,radio_connect_wait,playback_reached_end,resume_pending,seek_requested,video_file_direction;
static int scenario,starts,stops,connections,peeks;
static const char *tr(int id){(void)id;return "reconnect";}
static void lcd_music_reset(void){}
static void tv_music_reset(void){}
static void tv_draw_music(const char *s,int f){(void)s;(void)f;}
static void lcd_draw_music(const char *s,int f){(void)s;(void)f;}
static int music_remote_start(void){starts++;music_remote_action=0;return 0;}
static void music_remote_stop(void){stops++;music_remote_action=0;}
static void keep_awake(void){}
static int comfort_expired(void){return 0;}
static void sceKernelDelayThread(int us){tick+=us;assert(tick<30000000ULL);}
static void sceCtrlPeekBufferPositive(SceCtrlData *p,int n){
    (void)n;p->Buttons=0;peeks++;
    if(peeks<2)return;
    if(scenario==1)p->Buttons=PSP_CTRL_START;
    if(scenario==2)p->Buttons=PSP_CTRL_CROSS;
    if(scenario==3)music_remote_action=MUSIC_REMOTE_PLAY;
    if(scenario==4){music_remote_action=MUSIC_REMOTE_SEEK;music_remote_seconds=123;}
}
static int wait_for_network_restore(void){
    assert(radio_connect_wait);connections++;return scenario==5 && connections==1?-3:0;
}
#include "playback_recovery.h"
int main(void){
    struct sockaddr_in address={0};char data[16]={0};
    assert(playback_connect(1,&address,&running)==0 && nonblock);
    sends=0;assert(playback_send(1,data,10,&running)==10 && sends==4);
    mode=4;assert(playback_recv(1,data,16,250)==-2);
    tick=0;assert(playback_send(1,data,10,&running)==-1 && tick>=15000000ULL);
    mode=1;tick=0;assert(playback_connect(1,&address,&running)<0 && tick>=15000000ULL);
    mode=2;tick=0;running=1;assert(playback_connect(1,&address,&running)<0 && tick<=100000);
    mode=3;running=1;assert(playback_connect(1,&address,&running)<0);
    mode=0;server_https=1;assert(playback_connect(1,&address,&running)==0);
    assert(playback_send(1,data,10,&running)==10 && playback_recv(1,data,16,100)==-2);
    for(scenario=0;scenario<=5;scenario++){
        playback_recovery_reset();
        tick=0;starts=stops=connections=peeks=0;tv_ui_active=scenario&1;
        int expected=scenario!=1 && scenario!=3;
        assert(playback_reconnect_wait()==expected);
        assert(starts==stops && !radio_connect_wait);
        if(scenario==0)assert(tick>=5000000ULL && connections==1);
        if(scenario==2)assert(tick<5000000ULL);
        if(scenario==4)assert(playback_position_ms==123000);
        if(scenario==5)assert(connections==2 && tick>=10000000ULL);
    }
    playback_recovery_reset();tick=0;scenario=0;
    recovery_failures=3;assert(playback_recovery_associate()==0 && forced==1);
    assert(inputs_stopped && network_ready && http_ready);
    recovery_failures=3;tick=59999999;
    assert(playback_recovery_associate()==0 && forced==1);
    tick=60000000;
    assert(playback_recovery_associate()==0 && forced==2);
    assert(playback_recovery_position(456789)==456 && playback_recovery_position(-1)==0);
    playback_reached_end=resume_pending=seek_requested=video_file_direction=1;
    playback_recovery_cancel();assert(!playback_reached_end && !resume_pending && !seek_requested && !video_file_direction);
    puts("transport deadlines, cancellation, partial sends, TLS, retries, stop, remote play/seek: OK");
}

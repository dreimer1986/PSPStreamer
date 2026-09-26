#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {int pool,maximum,free;} SceNetMallocStat;
static int debug_enabled=1,open_failure,close_failure,fake_errno,logs;
static char last_event[64],last_detail[256];
static unsigned long long tick;
static unsigned long long sceKernelGetSystemTimeWide(void){return tick;}
static int sceNetInetSocket(int a,int b,int c){(void)a;(void)b;(void)c;fake_errno=55;return open_failure?-1:0;}
static int sceNetInetGetErrno(void){return fake_errno;}
int tls_close(int fd){assert(fd==0);fake_errno=9;return close_failure?-1:0;}
static int sceNetGetMallocStat(SceNetMallocStat *s){*s=(SceNetMallocStat){131072,65536,65536};fake_errno=123;return 0;}
static void recovery_log(const char *event,int result,int status,const char *detail){
    (void)result;(void)status;logs++;snprintf(last_event,sizeof(last_event),"%s",event);
    snprintf(last_detail,sizeof(last_detail),"%s",detail);
}
#include "socket_diagnostics.h"
int main(void){
    for(int i=0;i<10000;i++){assert(sceNetInetSocket(0,0,0)==0);assert(socket_tracked_close(0)==0);}
    assert(socket_opens==10000 && socket_closes==10000 && socket_live==0 && socket_peak==1);
    open_failure=1;assert(sceNetInetSocket(0,0,0)==-1);
    assert(socket_open_failures==1 && socket_live==0);
    assert(!strcmp(last_event,"socket allocation failed") && strstr(last_detail,"errno=55 hex=0x00000037"));
    assert(strstr(last_detail,"pool=131072") && strstr(last_detail,"free=65536"));
    open_failure=0;assert(sceNetInetSocket(0,0,0)==0);close_failure=1;
    assert(socket_tracked_close(0)==-1 && socket_live==1 && socket_close_failures==1);
    assert(strstr(last_detail,"errno=9 "));
    close_failure=0;assert(socket_tracked_close(0)==0 && !socket_live);
    int before=logs;socket_snapshot_tick();assert(logs==before+1);
    socket_snapshot_tick();assert(logs==before+1);
    tick=30000000ULL;socket_snapshot_tick();assert(logs==before+2);
    debug_enabled=0;open_failure=1;assert(sceNetInetSocket(0,0,0)==-1);
    tick+=30000000ULL;socket_snapshot_tick();assert(logs==before+2);
    return 0;
}

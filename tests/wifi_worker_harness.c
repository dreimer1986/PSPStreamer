#include <assert.h>
#include <stddef.h>
#include <string.h>
typedef unsigned int SceSize;
typedef struct {unsigned int Buttons;} SceCtrlData;
#define PSP_THREAD_ATTR_USER 0
#define PSP_CTRL_CIRCLE 1
#define PSP_CTRL_START 2
static volatile int wifi_worker_cancel;
static const char *wifi_worker_step,*failure_step;
static int network_ready,http_ready,radio_connect_wait,app_exit_requested,wifi_allow_apctl_restart;
static int created,deleted,blocked,buttons,cancel_at,create_error,start_error,core_result;
static unsigned long long now;
static int (*entry)(SceSize,void *);
static int wifi_initialize_core(void){return core_result;}
static int wifi_associate_core(int force){(void)force;return core_result;}
static int sceKernelCreateThread(const char *name,int (*fn)(SceSize,void *),int p,int s,int a,void *v){
    (void)name;(void)p;(void)s;(void)a;(void)v;
    if(create_error)return create_error;
    created++;entry=fn;return created;
}
static int sceKernelStartThread(int t,int a,void *v){(void)t;(void)a;(void)v;return start_error;}
static int sceKernelDeleteThread(int t){(void)t;deleted++;return 0;}
static int sceKernelWaitThreadEnd(int t,unsigned int *timeout){(void)t;(void)timeout;return 0;}
static unsigned long long sceKernelGetSystemTimeWide(void){return now;}
static void keep_awake(void){}
static void sceCtrlPeekBufferPositive(SceCtrlData *pad,int n){(void)n;pad->Buttons=buttons;}
static void recovery_log(const char *event,int r,int h,const char *stage){(void)event;(void)r;(void)h;(void)stage;}
static void sceKernelDelayThread(int us){
    now+=us;
    if(cancel_at && now>=(unsigned int)cancel_at)buttons=PSP_CTRL_START;
    if(!blocked && entry){int (*fn)(SceSize,void *)=entry;entry=NULL;fn(0,NULL);}
}
#include "wifi_worker.h"
int main(void){
    assert(wifi_associate(0)==0 && created==1 && deleted==1);
    blocked=1;cancel_at=60000;network_ready=http_ready=1;
    assert(wifi_associate(1)==-5 && now<100000 && wifi_worker_cancel);
    assert(!network_ready && !http_ready && created==2 && deleted==1);
    /* Re-entry while firmware is stuck must not create/delete any worker. */
    assert(wifi_associate(1)==-5 && created==2 && deleted==1);
    assert(strstr(failure_step,"busy"));
    entry(0,NULL);entry=NULL;blocked=buttons=cancel_at=0;
    assert(wifi_associate(0)==0 && created==3 && deleted==3);
    blocked=1;now=0;
    assert(wifi_associate(1)==-5 && now==60000000ULL && created==4 && deleted==3);
    assert(wifi_associate(0)==-5 && created==4);
    entry(0,NULL);entry=NULL;blocked=0;core_result=-99;
    assert(wifi_associate(0)==-99 && deleted==5);
    core_result=0;assert(wifi_associate(-1)==-1);
    create_error=-9;assert(wifi_associate(0)==-9);create_error=0;
    start_error=-8;assert(wifi_associate(0)==-8 && wifi_worker_thread==-1);
    return 0;
}

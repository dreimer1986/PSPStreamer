#include <assert.h>
#include <string.h>
#include "../streammaster/protocol.h"
static unsigned long long tick=1000000;
static int usb,active=1,rpcs,mode;
static unsigned long long sceKernelGetSystemTimeWide(void){return tick;}
static int stm_enabled(void){return usb;}
static int wifi_status_menu_active(void){return active;}
static void wifi_status_draw(int tv){(void)tv;}
#define PSP_NET_APCTL_INFO_SSID 2
#define PSP_NET_APCTL_INFO_STRENGTH 5
union SceNetApctlInfo {unsigned char ssid[32],strength;};
static int sceNetApctlGetState(int *state){*state=mode?0:4;return 0;}
static int sceNetApctlGetInfo(int key,union SceNetApctlInfo *info){
    memset(info,0,sizeof(*info));if(key==2)memcpy(info->ssid,"Heimnetz",8);else info->strength=60;return 0;
}
static int stm_rpc(unsigned op,const void *data,unsigned size,void *reply,unsigned cap,unsigned *length,volatile int *running){
    (void)data;(void)size;(void)cap;assert(op==SM_NETWORK_INFO);assert(*running);rpcs++;
    SmNetworkInfo *info=reply;memset(info,0,sizeof(*info));*length=sizeof(*info);
    info->info.wifi_state=mode?SM_WIFI_FAILED:SM_WIFI_READY;info->info.rssi=-62;
    memset(info->ssid,'A',33);return 0;
}
#include "wifi_status.h"
int main(void){
    volatile int running=1;wifi_status_collect(&running);
    assert(wifi_pending.connected&&wifi_pending.bars==3&&!strcmp(wifi_pending.ssid,"Heimnetz"));
    usb=1;wifi_status_collect(&running);assert(rpcs==0);
    tick+=5000000;wifi_status_collect(&running);assert(rpcs==1&&wifi_pending.usb&&wifi_pending.bars==3);
    assert(strlen(wifi_pending.ssid)==32);
    active=0;tick+=5000000;wifi_status_collect(&running);assert(rpcs==1);
    active=1;mode=1;wifi_status_collect(&running);assert(rpcs==2&&!wifi_pending.connected);
    wifi_status=wifi_pending;wifi_status_draw(0);assert(!wifi_status.connected);
    return 0;
}

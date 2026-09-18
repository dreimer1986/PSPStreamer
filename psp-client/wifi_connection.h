/* SPDX-License-Identifier: GPL-2.0-or-later
 * Infrastructure association only. Never unload network/AVC modules here.
 * wifi_wait_tick() waits 100 ms and returns nonzero on user cancellation. */
#ifndef PSPSTREAMER_WIFI_CONNECTION_H
#define PSPSTREAMER_WIFI_CONNECTION_H
static int wifi_init_stage;
static int wifi_initialize(void) {
    int result;
    while(wifi_init_stage<5) {
        switch(wifi_init_stage) {
        case 0: failure_step="Network Common";result=sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);break;
        case 1: failure_step="Network INET";result=sceUtilityLoadNetModule(PSP_NET_MODULE_INET);break;
        case 2: failure_step="sceNetInit";result=sceNetInit(128*1024,42,0,42,0);break;
        case 3: failure_step="sceNetInetInit";result=sceNetInetInit();break;
        default: failure_step="sceNetApctlInit";result=sceNetApctlInit(0x1800,48);break;
        }
        if(result<0)return result;
        wifi_init_stage++;
    }
    return 0;
}
static int wifi_associate(int force) {
    int result=wifi_initialize(),state=0,profile=PSP_NETWORK_PROFILE;
    if(result<0)return result;
    failure_step="Wi-Fi profile";
    if(!profile) {
        for(profile=1;profile<=100;profile++)if(sceUtilityCheckNetParam(profile)==0)break;
        if(profile>100)return -4;
    } else if((result=sceUtilityCheckNetParam(profile))<0)return result;
    active_network_profile=profile;
    failure_step="Wi-Fi state";
    if((result=sceNetApctlGetState(&state))<0)return result;
    if(!force && state==PSP_NET_APCTL_STATE_GOT_IP)return 0;
    /* A previous timed-out attempt may still be joining/authenticating.
     * Reach DISCONNECTED before issuing exactly one new connect request. */
    if(state!=0) {
        failure_step="Wi-Fi disconnect";
        if((result=sceNetApctlDisconnect())<0)return result;
        int attempts;
        for(attempts=0;attempts<30;attempts++) {
            if((result=sceNetApctlGetState(&state))<0)return result;
            if(!state)break;
            if(wifi_wait_tick())return -5;
        }
        if(state)return -6;
    }
    failure_step="Wi-Fi connection";
    if((result=sceNetApctlConnect(profile))<0)return result;
    for(int attempts=0;attempts<300;attempts++) {
        if((result=sceNetApctlGetState(&state))<0)return result;
        if(state==PSP_NET_APCTL_STATE_GOT_IP)return 0;
        if(wifi_wait_tick()) {sceNetApctlDisconnect();return -5;}
    }
    sceNetApctlDisconnect();
    return -3;
}
#endif

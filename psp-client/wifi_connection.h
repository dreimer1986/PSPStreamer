/* SPDX-License-Identifier: GPL-2.0-or-later
 * Infrastructure association only. Never unload network/AVC modules here.
 * wifi_wait_tick() waits 100 ms and returns nonzero on user cancellation. */
#ifndef PSPSTREAMER_WIFI_CONNECTION_H
#define PSPSTREAMER_WIFI_CONNECTION_H
static int wifi_init_stage;
static int wifi_rebuild_pending,wifi_disconnect_failures,wifi_apctl_restarted;
/* Only recovery, after joining network users, may rebuild APCTL itself. */
static int wifi_allow_apctl_restart;
static int wifi_last_state=-1;
static int wifi_read_state(int *state) {
    int result=sceNetApctlGetState(state);
    if(result<0 || *state!=wifi_last_state) {
        recovery_log("WLAN state",result,result<0?-1:*state,failure_step);
        if(result>=0)wifi_last_state=*state;
    }
    return result;
}
static int wifi_initialize(void) {
    int result;
    while(wifi_init_stage<5) {
        if(wifi_should_cancel())return -5;
        recovery_log("WLAN init begin",wifi_init_stage,0,"network initialization");
        switch(wifi_init_stage) {
        case 0: failure_step="Network Common";result=sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);break;
        case 1: failure_step="Network INET";result=sceUtilityLoadNetModule(PSP_NET_MODULE_INET);break;
        case 2: failure_step="sceNetInit";result=sceNetInit(128*1024,42,0,42,0);break;
        case 3: failure_step="sceNetInetInit";result=sceNetInetInit();break;
        default: failure_step="sceNetApctlInit";result=sceNetApctlInit(0x1800,48);break;
        }
        recovery_log("WLAN init end",result,wifi_init_stage,failure_step);
        if(result<0)return result;
        wifi_init_stage++;
    }
    return 0;
}
static int wifi_associate(int force) {
    if(force)wifi_rebuild_pending=1;
    wifi_last_state=-1;
    int result=wifi_initialize(),state=0,profile=PSP_NETWORK_PROFILE;
    if(result<0)return result;
    if(wifi_should_cancel())return -5;
    failure_step="Wi-Fi profile";
    if(!profile) {
        for(profile=1;profile<=100;profile++)if(sceUtilityCheckNetParam(profile)==0)break;
        if(profile>100)return -4;
    } else if((result=sceUtilityCheckNetParam(profile))<0)return result;
    if(wifi_should_cancel())return -5;
    active_network_profile=profile;
    failure_step="Wi-Fi state";
    if((result=wifi_read_state(&state))<0)return result;
    if(wifi_should_cancel())return -5;
    if(!wifi_rebuild_pending && state==PSP_NET_APCTL_STATE_GOT_IP)return 0;
    /* A previous timed-out attempt may still be joining/authenticating.
     * Reach DISCONNECTED before issuing exactly one new connect request. */
    if(state!=0) {
        wifi_rebuild_pending=1;
        failure_step="Wi-Fi disconnect";
        recovery_log("APCTL disconnect begin",0,state,failure_step);
        result=sceNetApctlDisconnect();
        recovery_log("APCTL disconnect end",result,state,failure_step);
        if(wifi_should_cancel())return -5;
        if(result<0)return result;
        int attempts;
        for(attempts=0;attempts<100;attempts++) {
            failure_step="Wi-Fi disconnect state";
            if((result=wifi_read_state(&state))<0)return result;
            if(wifi_should_cancel())return -5;
            if(!state)break;
            if(wifi_wait_tick())return -5;
        }
        if(state) {
            wifi_disconnect_failures++;
            if(!wifi_allow_apctl_restart || wifi_disconnect_failures<2 || wifi_apctl_restarted)return -6;
            failure_step="Wi-Fi APCTL restart";
            recovery_log("APCTL terminate begin",0,state,failure_step);
            result=sceNetApctlTerm();
            recovery_log("APCTL terminate",result,state,failure_step);
            if(result<0)return result;
            wifi_init_stage=4;wifi_apctl_restarted=1;
            if(wifi_should_cancel())return -5;
            result=wifi_initialize();
            recovery_log("APCTL initialize",result,0,failure_step);
            if(result<0)return result;
            if((result=wifi_read_state(&state))<0)return result;
            if(state)return -6;
        }
    }
    wifi_rebuild_pending=1;
    if(wifi_should_cancel())return -5;
    failure_step="Wi-Fi connection";
    recovery_log("APCTL connect begin",0,profile,failure_step);
    result=sceNetApctlConnect(profile);
    recovery_log("APCTL connect end",result,profile,failure_step);
    if(wifi_should_cancel())return -5;
    if(result<0)return result;
    for(int attempts=0;attempts<300;attempts++) {
        if((result=wifi_read_state(&state))<0)return result;
        if(wifi_should_cancel())return -5;
        if(state==PSP_NET_APCTL_STATE_GOT_IP) {
            wifi_rebuild_pending=wifi_disconnect_failures=wifi_apctl_restarted=0;
            return 0;
        }
        if(wifi_wait_tick())return -5;
    }
    /* Leave pending ownership to the next attempt; do not block cancellation
     * on another firmware call after the association budget has expired. */
    return -3;
}
#endif

#ifndef PSP_WIFI_STATUS_H
#define PSP_WIFI_STATUS_H
/* GUI worker owns pending status; GUI consumes it only after worker join.
 * No scans, HTTP requests or USB transactions from a drawing function. */
typedef struct { char ssid[33]; int bars, connected, usb; unsigned long long stamp; } WifiStatus;
static WifiStatus wifi_status,wifi_pending;
static unsigned long long wifi_status_next;
static void wifi_status_draw(int tv);
static int wifi_status_menu_active(void);
static void wifi_status_collect(volatile int *running) {
    unsigned long long now=sceKernelGetSystemTimeWide();
    if(now<wifi_status_next || !wifi_status_menu_active())return;
    wifi_status_next=now+5000000ULL;
    WifiStatus next;memset(&next,0,sizeof(next));next.stamp=now;next.usb=stm_enabled();
    if(next.usb) {
        SmNetworkInfo info;unsigned length=0;memset(&info,0,sizeof(info));
        int rc=stm_rpc(SM_NETWORK_INFO,NULL,0,&info,sizeof(info),&length,running);
        if(rc>=0 && length==sizeof(info) && info.info.wifi_state==SM_WIFI_READY) {
            memcpy(next.ssid,info.ssid,32);next.connected=1;
            int rssi=info.info.rssi;
            next.bars=rssi>=-55?4:rssi>=-67?3:rssi>=-75?2:1;
        }
    } else {
        int state=0;union SceNetApctlInfo info;
        if(sceNetApctlGetState(&state)>=0 && state==4 &&
           sceNetApctlGetInfo(PSP_NET_APCTL_INFO_SSID,&info)>=0) {
            memcpy(next.ssid,info.ssid,32);next.connected=1;
            if(sceNetApctlGetInfo(PSP_NET_APCTL_INFO_STRENGTH,&info)>=0)
                next.bars=info.strength>=75?4:info.strength>=50?3:info.strength>=25?2:1;
        }
    }
    wifi_pending=next;
}
#endif

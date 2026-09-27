/* SPDX-License-Identifier: GPL-2.0-or-later
 * Setup/diagnostics share transport ownership with the app. The UI never
 * blocks in USB, and leaving this menu keeps an active USB transport alive. */
#include "../streammaster/protocol.h"
#include "streammaster_fields.h"
enum {SM_JOB_ATTACH=100,SM_JOB_BENCH,SM_JOB_SERVER};
static int sm_thread=-1;
static volatile int sm_running;
static volatile int sm_cancel,sm_finished,sm_result,sm_progress;
static int sm_job,sm_http_status;
static unsigned sm_rate,sm_bytes;
static SmFrame sm_response;
static SmConfig sm_draft;
static SmInfo sm_info;
static SmNetworkInfo sm_network;
static SmScan sm_scan;
static SmHttpOpen sm_server;
static const char *sm_stage="idle";
static int sm_step(const char *stage,int rc) {
    sm_stage=stage;
    recovery_log("StreamMaster",rc,0,stage);
    return rc;
}

static int sm_rpc(unsigned op,const void *data,unsigned size) {
    if(sm_cancel)return SM_TIMEOUT;
    if(size>SM_PAYLOAD_SIZE)return SM_INVALID;
    memset(&sm_response,0,sizeof(sm_response));
    sm_stage="USB exchange";
    unsigned length=0;
    int rc=stm_rpc(op,data,size,sm_response.payload,sizeof(sm_response.payload),&length,&sm_running);
    sm_response.length=length;return rc;
}
static int sm_read_info(void) {
    int rc=sm_rpc(SM_NETWORK_INFO,NULL,0);
    if(rc==SM_INVALID)rc=sm_rpc(SM_INFO,NULL,0); /* Firmware <= 0.1.1. */
    if(rc<0)return rc;
    if(sm_response.length!=sizeof(sm_info) && sm_response.length!=sizeof(sm_network))return SM_IO;
    memset(&sm_network,0,sizeof(sm_network));
    if(sm_response.length==sizeof(sm_network))memcpy(&sm_network,sm_response.payload,sizeof(sm_network));
    memcpy(&sm_info,sm_response.payload,sizeof(sm_info));
    sm_info.firmware[31]=sm_info.ip[15]=sm_info.gateway[15]=sm_info.dns[15]=sm_info.dns2[15]=0;
    sm_network.info=sm_info;sm_network.mask[15]=sm_network.ssid[32]=0;
    return 0;
}
static int sm_load(void) {
    int rc=stm_driver_start(0,&sm_running);
    sm_step(stm_stage(),rc);
    return rc<0?rc:sm_read_info();
}
static int sm_worker(SceSize size,void *args) {
    (void)size;(void)args;int rc=0;
    if(sm_job==SM_JOB_ATTACH) {
        rc=sm_load();
        if(rc>=0) {
            rc=sm_rpc(SM_CONFIG_GET,NULL,0);
            if(rc>=0) {
                if(sm_response.length!=sizeof(sm_draft))rc=SM_IO;
                else {memcpy(&sm_draft,sm_response.payload,sizeof(sm_draft));
                    sm_draft.flags=(sm_draft.flags&(SM_CFG_DHCP|SM_CFG_AUTO_DNS))|SM_CFG_KEEP_PASSWORD;
                    memset(sm_draft.password,0,sizeof(sm_draft.password));
                    sm_draft.ssid[32]=sm_draft.ip[15]=sm_draft.mask[15]=sm_draft.gateway[15]=sm_draft.dns[15]=sm_draft.dns2[15]=0;}
            }
        }
    } else if(sm_job==SM_JOB_BENCH) {
        static unsigned char pattern[SM_PAYLOAD_SIZE];
        unsigned long long start=sceKernelGetSystemTimeWide();
        for(unsigned i=0;i<128 && !sm_cancel;i++) {
            for(unsigned j=0;j<sizeof(pattern);j++)pattern[j]=(unsigned char)(i*37+j*13);
            rc=sm_rpc(SM_ECHO,pattern,sizeof(pattern));
            if(rc<0)break;
            if(sm_response.length!=sizeof(pattern)||memcmp(sm_response.payload,pattern,sizeof(pattern))){rc=SM_IO;break;}
            sm_bytes+=sizeof(pattern);sm_progress=(i+1)*100/128;
        }
        unsigned long long elapsed=sceKernelGetSystemTimeWide()-start;
        sm_rate=elapsed?(unsigned)((unsigned long long)sm_bytes*1000000ULL/elapsed/1024):0;
        char measurement[128];
        snprintf(measurement,sizeof(measurement),"KiB_s=%u bytes=%u elapsed_ms=%llu",sm_rate,sm_bytes,elapsed/1000);
        recovery_log("StreamMaster benchmark",rc,0,measurement);
    } else if(sm_job==SM_JOB_SERVER) {
        rc=sm_rpc(SM_HTTP_OPEN,&sm_server,sizeof(sm_server));
        if(rc>=0) {
            if(sm_response.length!=sizeof(SmHttpResult))rc=SM_IO;
            else {SmHttpResult result;memcpy(&result,sm_response.payload,sizeof(result));sm_http_status=result.status;}
        }
        SceInt64 deadline=sceKernelGetSystemTimeWide()+10000000;
        while(rc>=0 && !sm_cancel && sm_bytes<65536) {
            if(sceKernelGetSystemTimeWide()>=deadline){rc=SM_TIMEOUT;break;}
            rc=sm_rpc(SM_HTTP_READ,NULL,0);
            if(rc==SM_BUSY){rc=0;sceKernelDelayThread(20000);continue;}
            if(rc<0 || !sm_response.length)break;
            sm_bytes+=sm_response.length;
        }
        if(!sm_cancel)sm_rpc(SM_HTTP_CLOSE,NULL,0);
        if(rc>=0 && (sm_http_status!=200 || !sm_bytes))rc=SM_IO;
    } else if(sm_job==SM_CONFIG_SET) {
        rc=sm_rpc(SM_CONFIG_SET,&sm_draft,sizeof(sm_draft));
        if(rc>=0){memset(sm_draft.password,0,sizeof(sm_draft.password));sm_draft.flags|=SM_CFG_KEEP_PASSWORD;memset(&sm_network,0,sizeof(sm_network));}
    } else if(sm_job==SM_INFO)rc=sm_read_info();
    else if(sm_job==SM_SCAN) {
        rc=sm_rpc(SM_SCAN,NULL,0);
        if(rc>=0) {
            if(sm_response.length!=sizeof(sm_scan))rc=SM_IO;
            else {memcpy(&sm_scan,sm_response.payload,sizeof(sm_scan));if(sm_scan.count>24)rc=SM_IO;
                for(unsigned i=0;i<24;i++)sm_scan.ap[i].ssid[32]=0;}
        }
    } else rc=sm_rpc(sm_job,NULL,0);
    memset(&sm_server,0,sizeof(sm_server));memset(&sm_response,0,sizeof(sm_response));
    sm_result=sm_cancel?SM_TIMEOUT:rc;
    recovery_log("StreamMaster job done",sm_result,sm_http_status,sm_stage);
    sm_finished=1;
    return 0;
}
static int sm_reap(void) {
    if(sm_thread<0)return 0;
    SceUInt timeout=1000;
    if(sceKernelWaitThreadEnd(sm_thread,&timeout)<0)return SM_BUSY;
    sceKernelDeleteThread(sm_thread);sm_thread=-1;return 0;
}
static void streammaster_cleanup(void) {
    sm_cancel=1;
    sm_running=0;
    if(sm_thread>=0)stm_driver_cancel();
    /* Never terminate a worker or free kernel DMA storage while it is owned
     * by USB. On an abnormal driver timeout keep the module until app exit. */
    for(int i=0;i<25 && sm_reap()<0;i++)sceKernelDelayThread(10000);
    if(!stm_enabled() && sm_thread<0)stm_driver_stop();
}
static int sm_run(int job) {
    if(sm_reap()<0)return SM_BUSY;
    sm_job=job;sm_cancel=sm_finished=sm_progress=0;sm_bytes=sm_rate=0;sm_http_status=0;
    sm_running=1;
    if(job==SM_JOB_SERVER) {
        memset(&sm_server,0,sizeof(sm_server));
        snprintf(sm_server.url,sizeof(sm_server.url),"%s://%s:%d/api/health",server_https?"https":"http",server_host,server_port);
        const char *auth=strstr(server_auth_header,"Authorization: ");
        if(auth){snprintf(sm_server.authorization,sizeof(sm_server.authorization),"%s",auth+15);
            sm_server.authorization[strcspn(sm_server.authorization,"\r\n")]=0;}
    }
    sm_thread=sceKernelCreateThread("StreamMaster RPC",sm_worker,0x18,16384,PSP_THREAD_ATTR_USER,NULL);
    if(sm_thread<0)return sm_thread;
    int rc=sceKernelStartThread(sm_thread,0,NULL);
    if(rc<0){sceKernelDeleteThread(sm_thread);sm_thread=-1;return rc;}
    unsigned old=PSP_CTRL_CROSS;unsigned long long next=0,cancel_deadline=0;
    while(!sm_finished) {
        keep_awake();SceCtrlData pad;sceCtrlReadBufferPositive(&pad,1);
        unsigned long long now=sceKernelGetSystemTimeWide();
        if((pad.Buttons&~old)&PSP_CTRL_CIRCLE) {
            sm_cancel=1;cancel_deadline=now+1000000ULL;
            sm_running=0;stm_driver_cancel();
        }
        if(cancel_deadline && now>=cancel_deadline)return SM_TIMEOUT;
        if(now>=next) {
            char line[64];settings_shell("StreamMaster");
            settings_line(0,0,tr(TXT_SM_WAIT));
            snprintf(line,sizeof(line),"USB: %d%%",sm_progress);settings_line(2,0,line);
            settings_help(tr(TXT_SM_CANCEL));next=now+250000;
        }
        old=pad.Buttons;sceKernelDelayThread(20000);
    }
    rc=sm_result;sm_reap();return rc;
}
static void sm_choose_ap(void) {
    if(!sm_scan.count)return;
    unsigned selected=0,old=PSP_CTRL_CROSS;int dirty=1;
    while(1) {
        keep_awake();SceCtrlData pad;sceCtrlReadBufferPositive(&pad,1);unsigned pressed=pad.Buttons&~old;
        if(dirty) {
            settings_shell(tr(TXT_SM_SCAN));
            for(unsigned i=selected/8*8;i<sm_scan.count && i<selected/8*8+8;i++) {
                char line[72];snprintf(line,sizeof(line),"%c %.32s %d dBm",selected==i?'>':' ',sm_scan.ap[i].ssid,sm_scan.ap[i].rssi);
                settings_line(i%8,i==selected,line);
            }
            settings_help(tr(TXT_SM_HELP));dirty=0;
        }
        if(pressed&PSP_CTRL_CIRCLE)return;
        if((pressed&PSP_CTRL_CROSS) && sm_scan.ap[selected].ssid[0]) {
            if(strcmp(sm_draft.ssid,sm_scan.ap[selected].ssid)) {
                strcpy(sm_draft.ssid,sm_scan.ap[selected].ssid);memset(sm_draft.password,0,sizeof(sm_draft.password));
                sm_draft.flags&=~SM_CFG_KEEP_PASSWORD;
            }
            return;
        }
        if(pressed&PSP_CTRL_UP){selected=(selected+sm_scan.count-1)%sm_scan.count;dirty=1;}
        if(pressed&PSP_CTRL_DOWN){selected=(selected+1)%sm_scan.count;dirty=1;}
        old=pad.Buttons;sceKernelDelayThread(20000);
    }
}
static void streammaster_settings(void) {
    enum {M_ATTACH,M_INFO,M_SCAN,M_SSID,M_PASSWORD,M_DHCP,M_AUTO_DNS,M_IP,M_MASK,M_GATEWAY,M_DNS,M_DNS2,M_SAVE,M_CONNECT,M_BENCH,M_SERVER,M_COUNT};
    static const TextId labels[M_COUNT]={TXT_SM_ATTACH,TXT_SM_INFO,TXT_SM_SCAN,TXT_SM_SSID,TXT_SM_PASSWORD,
        TXT_SM_DHCP,TXT_SM_AUTO_DNS,TXT_SM_IP,TXT_SM_MASK,TXT_SM_GATEWAY,TXT_SM_DNS,TXT_SM_DNS2,
        TXT_SM_SAVE,TXT_SM_CONNECT,TXT_SM_BENCH,TXT_SM_SERVER};
    int selected=0,dirty=1,rc=0,ready=0;char detail[80]="";
    unsigned old=PSP_CTRL_CROSS;unsigned long long repeat=0;
    /* Reload the ESP's persisted configuration and current DHCP lease on
     * every visit; clearing the local password draft on exit is intentional. */
    rc=sm_run(SM_JOB_ATTACH);
    if(rc>=0)ready=1;
    else snprintf(detail,sizeof(detail),"%s",sm_stage);
    while(1) {
        keep_awake();SceCtrlData pad;sceCtrlReadBufferPositive(&pad,1);unsigned pressed=pad.Buttons&~old;
        if(dirty) {
            settings_shell("StreamMaster / Onju V3");
            for(int i=selected/8*8;i<M_COUNT && i<selected/8*8+8;i++) {
                char value[40]="[X]",line[88];
                if(i==M_SSID)snprintf(value,sizeof(value),"%s",sm_draft.ssid);
                if(i==M_PASSWORD)snprintf(value,sizeof(value),"%s",tr(sm_draft.flags&SM_CFG_KEEP_PASSWORD?TXT_SM_RETAIN:sm_draft.password[0]?TXT_SM_HIDDEN:TXT_OFF));
                if(i==M_DHCP || i==M_AUTO_DNS)snprintf(value,sizeof(value),"%s",tr(sm_draft.flags&(i==M_DHCP?SM_CFG_DHCP:SM_CFG_AUTO_DNS)?TXT_SETTINGS_ON:TXT_OFF));
                if(i>=M_IP && i<=M_DNS2)sm_field_value(value,sizeof(value),&sm_draft,&sm_network,i-M_IP);
                snprintf(line,sizeof(line),"%c %s: %s",selected==i?'>':' ',tr(labels[i]),value);
                settings_line(i%8,selected==i,line);
            }
            char message[80];
            if(rc<0)snprintf(message,sizeof(message),"%s: %08X %.40s",tr(TXT_SM_ERROR),(unsigned)rc,detail);
            else snprintf(message,sizeof(message),"%s",detail[0]?detail:tr(TXT_SM_TEST_ONLY));
            settings_line(8,0,message);settings_help(tr(TXT_SM_HELP));dirty=0;
        }
        if(pressed&PSP_CTRL_CIRCLE)break;
        if(pressed&PSP_CTRL_CROSS) {
            rc=0;detail[0]=0;
            if(selected==M_ATTACH){ready=0;rc=sm_run(SM_JOB_ATTACH);if(rc>=0)ready=1;}
            else if(!ready)rc=SM_OFFLINE;
            else if(sm_reap()<0)rc=SM_BUSY;
            else if(selected==M_INFO) {
                rc=sm_run(SM_INFO);
                if(rc>=0)snprintf(detail,sizeof(detail),"%s / %s / %d dBm",tr((TextId)(TXT_SM_IDLE+sm_info.wifi_state%4)),sm_info.ip,(int)sm_info.rssi);
            } else if(selected==M_SCAN){rc=sm_run(SM_SCAN);if(rc>=0){if(sm_scan.count)sm_choose_ap();else snprintf(detail,sizeof(detail),"%s",tr(TXT_SM_NO_AP));}}
            else if(selected==M_DHCP){sm_draft.flags^=SM_CFG_DHCP;if(!(sm_draft.flags&SM_CFG_DHCP))sm_draft.flags&=~SM_CFG_AUTO_DNS;}
            else if(selected==M_AUTO_DNS){if(sm_draft.flags&SM_CFG_DHCP)sm_draft.flags^=SM_CFG_AUTO_DNS;else rc=SM_INVALID;}
            else if(selected==M_SAVE) {
                if(!sm_config_valid(&sm_draft,1))rc=SM_INVALID;
                else {rc=sm_run(SM_CONFIG_SET);if(rc>=0)snprintf(detail,sizeof(detail),"%s",tr(TXT_SM_SAVED));}
            } else if(selected==M_CONNECT){rc=sm_run(SM_CONNECT);memset(&sm_network,0,sizeof(sm_network));}
            else if(selected==M_BENCH) {
                rc=sm_run(SM_JOB_BENCH);
                if(rc>=0)snprintf(detail,sizeof(detail),"USB: %u KiB/s / %u KiB OK",sm_rate,sm_bytes/1024);
            } else if(selected==M_SERVER) {
                rc=sm_run(SM_JOB_SERVER);
                /* Show HTTP status even on an authentication/server error. */
                if(sm_http_status)snprintf(detail,sizeof(detail),"HTTP %d / %u bytes",sm_http_status,sm_bytes);
            } else if(selected>=M_IP && selected<=M_DNS2 && sm_field_dhcp(&sm_draft,selected-M_IP)) {
                rc=sm_run(SM_INFO); /* Read-only lease field: X refreshes it. */
            } else {
                char draft[80],*field=selected==M_SSID?sm_draft.ssid:selected==M_PASSWORD?sm_draft.password:
                    selected==M_IP?sm_draft.ip:selected==M_MASK?sm_draft.mask:selected==M_GATEWAY?sm_draft.gateway:selected==M_DNS?sm_draft.dns:sm_draft.dns2;
                snprintf(draft,sizeof(draft),"%s",field);
                if(settings_text(draft,selected==M_SSID?33:selected==M_PASSWORD?65:16,selected==M_PASSWORD,tr(labels[selected]))) {
                    if(selected==M_SSID && strcmp(draft,field)){memset(sm_draft.password,0,sizeof(sm_draft.password));sm_draft.flags&=~SM_CFG_KEEP_PASSWORD;}
                    if(selected==M_PASSWORD)sm_draft.flags&=~SM_CFG_KEEP_PASSWORD;
                    strcpy(field,draft);
                }
                memset(draft,0,sizeof(draft));
            }
            if(rc<0 && !detail[0])snprintf(detail,sizeof(detail),"%s",sm_stage);
            if(rc==SM_IO || rc==SM_TIMEOUT)ready=0;
            dirty=1;sceCtrlReadBufferPositive(&pad,1);old=pad.Buttons;continue;
        }
        unsigned movement=pad.Buttons&(PSP_CTRL_UP|PSP_CTRL_DOWN);
        unsigned long long now=sceKernelGetSystemTimeWide();
        if(movement && ((pressed&movement)||now>=repeat)) {
            selected=(selected+(movement&PSP_CTRL_UP?M_COUNT-1:1))%M_COUNT;
            repeat=now+((pressed&movement)?400000:150000);dirty=1;
        }
        old=pad.Buttons;sceKernelDelayThread(20000);
    }
    streammaster_cleanup();
    if(sm_thread<0)memset(&sm_draft,0,sizeof(sm_draft));
}

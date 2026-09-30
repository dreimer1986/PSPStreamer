/* SPDX-License-Identifier: GPL-2.0-or-later
 * Setup/diagnostics share transport ownership with the app. The UI never
 * blocks in USB, and leaving this menu keeps an active USB transport alive. */
#include "../streammaster/protocol.h"
#include "../streammaster/gamepad_options.h"
#include "../streammaster/gamepad_learn.h"
#include "streammaster_fields.h"
enum {SM_JOB_ATTACH=100,SM_JOB_BENCH,SM_JOB_SERVER};
static int sm_thread=-1;
static volatile int sm_running;
static volatile int sm_cancel,sm_finished,sm_result,sm_progress;
static int sm_job,sm_http_status;
static unsigned sm_rate,sm_bytes;
static SmFrame sm_response;
static SmConfig sm_draft;
static SmProfiles sm_profiles;
static int sm_profiles_supported;
static uint32_t sm_slot,sm_profile_value;
static SmInfo sm_info;
static SmNetworkInfo sm_network;
static SmScan sm_scan;
static SmBtStatus sm_bt_status;
static SmBtAction sm_bt_action;
static SmBtUsbDiag sm_bt_usb_diag;
static SmBtOptions sm_bt_options;
static SmBtSetup sm_bt_setup;
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
static void sm_profile_draft(void) {
    sm_draft=sm_profiles.slot[sm_slot];
    if(!sm_draft.ssid[0])sm_draft.flags=SM_CFG_DHCP|SM_CFG_AUTO_DNS;
    else sm_draft.flags=(sm_draft.flags&(SM_CFG_DHCP|SM_CFG_AUTO_DNS))|SM_CFG_KEEP_PASSWORD;
    memset(sm_draft.password,0,sizeof(sm_draft.password));
    memset(&sm_network,0,sizeof(sm_network));
}
static int sm_read_profiles(void) {
    int rc=sm_rpc(SM_PROFILES_GET,NULL,0);
    if(rc<0)return rc;
    if(sm_response.length!=sizeof(sm_profiles))return SM_IO;
    memcpy(&sm_profiles,sm_response.payload,sizeof(sm_profiles));
    if(sm_profiles.version!=1 || sm_profiles.active>=SM_PROFILE_COUNT || sm_profiles.automatic>1)return SM_IO;
    for(unsigned i=0;i<SM_PROFILE_COUNT;i++) {
        SmConfig *c=&sm_profiles.slot[i];
        c->ssid[32]=c->ip[15]=c->mask[15]=c->gateway[15]=c->dns[15]=c->dns2[15]=0;
        memset(c->password,0,sizeof(c->password));
    }
    return 0;
}
static int sm_worker(SceSize size,void *args) {
    (void)size;(void)args;int rc=0;
    if(sm_job==SM_JOB_ATTACH) {
        sm_profiles_supported=0;sm_slot=0;memset(&sm_profiles,0,sizeof(sm_profiles));
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
        if(rc>=0) {
            int pr=sm_read_profiles();
            if(pr>=0){sm_profiles_supported=1;sm_slot=sm_profiles.active;sm_profile_draft();rc=sm_read_info();}
            else if(pr!=SM_INVALID)rc=pr; /* Old firmware still supports one network. */
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
    } else if(sm_job==SM_PROFILE_SAVE) {
        SmProfileSave request={.slot=sm_slot,.config=sm_draft};
        rc=sm_rpc(SM_PROFILE_SAVE,&request,sizeof(request));memset(&request,0,sizeof(request));
        if(rc>=0){rc=sm_read_profiles();if(rc>=0)sm_profile_draft();}
    } else if(sm_job==SM_PROFILE_SELECT || sm_job==SM_PROFILE_DELETE || sm_job==SM_PROFILE_AUTO) {
        rc=sm_rpc(sm_job,&sm_profile_value,sizeof(sm_profile_value));
        if(rc>=0){rc=sm_read_profiles();if(rc>=0)sm_profile_draft();}
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
    } else if(sm_job==SM_BT_SETUP_GET || sm_job==SM_BT_SETUP_SET) {
        rc=sm_rpc(sm_job,sm_job==SM_BT_SETUP_SET?&sm_bt_setup:NULL,sm_job==SM_BT_SETUP_SET?sizeof(sm_bt_setup):0);
        if(rc>=0 && sm_job==SM_BT_SETUP_GET) {
            if(sm_response.length!=sizeof(sm_bt_setup))rc=SM_IO;
            else {memcpy(&sm_bt_setup,sm_response.payload,sizeof(sm_bt_setup));
                if(sm_bt_setup.version!=1 || sm_bt_setup.reconnect>1 || !sm_bt_profile_valid(&sm_bt_setup.profile))rc=SM_INVALID;}
        }
    } else if(sm_job==SM_BT_OPTIONS_GET || sm_job==SM_BT_OPTIONS_SET) {
        rc=sm_rpc(sm_job,sm_job==SM_BT_OPTIONS_SET?&sm_bt_options:NULL,sm_job==SM_BT_OPTIONS_SET?sizeof(sm_bt_options):0);
        if(rc>=0 && sm_job==SM_BT_OPTIONS_GET) {
            if(sm_response.length!=sizeof(sm_bt_options))rc=SM_IO;
            else {memcpy(&sm_bt_options,sm_response.payload,sizeof(sm_bt_options));if(!sm_bt_options_valid(&sm_bt_options))rc=SM_INVALID;}
        }
    } else if(sm_job==SM_BT_STATUS || sm_job==SM_BT_ACTION) {
        if(sm_job==SM_BT_ACTION)rc=sm_rpc(SM_BT_ACTION,&sm_bt_action,sizeof(sm_bt_action));
        if(rc>=0)rc=sm_rpc(SM_BT_STATUS,NULL,0);
        if(rc>=0) {
            if(sm_response.length!=sizeof(sm_bt_status))rc=SM_IO;
            else {memcpy(&sm_bt_status,sm_response.payload,sizeof(sm_bt_status));
                if(sm_bt_status.count>8 || sm_bt_status.state>SM_BT_ERROR)rc=SM_IO;
                for(unsigned i=0;i<8;i++)sm_bt_status.device[i].name[47]=0;}
            if(rc>=0) {
                char detail[128];snprintf(detail,sizeof(detail),"adapter=%04X:%04X state=%u devices=%u reports=%u error=%08X",
                    (unsigned)sm_bt_status.vid,(unsigned)sm_bt_status.pid,(unsigned)sm_bt_status.state,
                    (unsigned)sm_bt_status.count,(unsigned)sm_bt_status.reports,(unsigned)sm_bt_status.error);
                recovery_log("StreamMaster Bluetooth",sm_bt_status.error,0,detail);
            }
        }
        /* Optional diagnostic extension: old firmware may reject it. Do not
         * replace the original action/status result with this extra query. */
        if(!sm_cancel && sm_rpc(SM_BT_USB_DIAG,NULL,0)>=0 && sm_response.length==sizeof(sm_bt_usb_diag)) {
            memcpy(&sm_bt_usb_diag,sm_response.payload,sizeof(sm_bt_usb_diag));
            recovery_log("StreamMaster PSP USB",(int32_t)sm_bt_usb_diag.reserved,0,"last PSP interface claim / offline");
            if(sm_bt_usb_diag.count<=8)for(unsigned i=0;i<sm_bt_usb_diag.count;i++) {
                const SmBtUsbProbe *p=&sm_bt_usb_diag.probe[i];char detail[192];
                snprintf(detail,sizeof(detail),"addr=%u usb=%04X:%04X phase=%u rc=%08X devclass=%06X iface=%08X ep=%06X attempts=%u",
                    (unsigned)p->address,(unsigned)p->vid,(unsigned)p->pid,(unsigned)p->phase,(unsigned)p->result,
                    (unsigned)p->device_class,(unsigned)p->interface_class,(unsigned)p->endpoints,(unsigned)p->attempts);
                recovery_log("StreamMaster BT USB",p->result,0,detail);
            }
            if(!sm_bt_usb_diag.count)recovery_log("StreamMaster BT USB",0,0,"no enumeration probes recorded");
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
/* One menu-local async snapshot, no persistent background poller. Separate
 * storage prevents the renderer seeing a partially received device list. */
static SmBtStatus sm_bt_refresh;
static int sm_bt_refresh_worker(SceSize args,void *argp) {
    (void)args;(void)argp;unsigned length=0;
    int rc=stm_rpc(SM_BT_STATUS,NULL,0,&sm_bt_refresh,sizeof(sm_bt_refresh),&length,&sm_running);
    if(rc>=0 && (length!=sizeof(sm_bt_refresh) || sm_bt_refresh.count>8 || sm_bt_refresh.state>SM_BT_ERROR))rc=SM_IO;
    for(unsigned i=0;i<8;i++)sm_bt_refresh.device[i].name[47]=0;
    sm_result=rc;__sync_synchronize();sm_finished=1;return 0;
}
static const char *sm_bt_target_name(unsigned target) {
    static const char *names[]={"X","O","[]","/\\","L","R","Select","Start"};
    static const TextId directions[]={TXT_SM_BT_UP,TXT_SM_BT_RIGHT,TXT_SM_BT_DOWN,TXT_SM_BT_LEFT};
    return target<8?names[target]:target<12?tr(directions[target-8]):tr(TXT_SM_BT_ANALOG);
}
static int sm_bt_learn(unsigned target) {
    unsigned old=~0U;SceInt64 started=sceKernelGetSystemTimeWide(),released=0,next_draw=0;
    SmBtAxisLearn axes={0};int armed=0;
    for(;;) {
        keep_awake();SceCtrlData p;sceCtrlReadBufferPositive(&p,1);unsigned pressed=p.Buttons&~old;
        if(pressed&PSP_CTRL_CIRCLE)return -1;
        if(pressed&PSP_CTRL_SQUARE)return 0; /* Keep previous assignment. */
        SmPad live={0};sceIoDevctl("stm:",SM_DEV_GAMEPAD,NULL,0,&live,sizeof(live));
        SceInt64 now=sceKernelGetSystemTimeWide();
        int connected=live.magic==SM_PAD_MAGIC && live.connected;
        if(connected && live.session!=sm_bt_setup.session)return -1;
        if(!connected){armed=0;released=0;memset(&axes,0,sizeof(axes));}
        else if(target<12) {
            if(!armed) {
                if(!live.raw_buttons && !live.hat){if(!released)released=now;if(now-released>=150000)armed=1;}
                else released=0;
            } else {
                unsigned source=sm_bt_single_source(&live);
                if(source){sm_bt_assign(&sm_bt_setup.profile,target,source);return 1;}
            }
        } else {
            uint8_t x,y;
            if(sm_bt_learn_axes(&axes,&live,(now-started)/1000,&x,&y)) {
                sm_bt_setup.profile.axis_x=x;sm_bt_setup.profile.axis_y=y;sm_bt_setup.profile.invert=0;return 1;
            }
        }
        if(now>=next_draw) {
            settings_shell(tr(TXT_SM_BT_WIZARD));
            char title[64];snprintf(title,sizeof(title),"%u/13: %s",target+1,sm_bt_target_name(target));settings_line(0,1,title);
            if(!connected)settings_line(2,0,tr(TXT_SM_BT_NEED_CONNECTED));
            else if(target<12){settings_line(2,0,tr(armed?TXT_SM_BT_PRESS:TXT_SM_BT_RELEASE));settings_line(3,1,sm_bt_target_name(target));}
            else {settings_line(2,0,tr(TXT_SM_BT_CIRCLE_STICK));settings_line(3,0,tr(TXT_SM_BT_CENTER_STICK));}
            settings_help(tr(TXT_SM_BT_LEARN_HELP));next_draw=now+200000;
        }
        old=p.Buttons;sceKernelDelayThread(20000);
    }
}
static void sm_bt_wizard(void) {
    for(unsigned i=0;i<13;i++)if(sm_bt_learn(i)<0)return;
    sm_bt_setup.profile.configured=1;
}
static void streammaster_bt_options(void) {
    int rc=sm_run(SM_BT_SETUP_GET);
    if(rc<0) {
        settings_shell(tr(TXT_SM_BT_OPTIONS));settings_line(1,0,tr(TXT_SM_BT_NEED_CONNECTED));
        settings_help(tr(TXT_SM_CANCEL));unsigned old=PSP_CTRL_CROSS;
        for(;;){SceCtrlData p;sceCtrlReadBufferPositive(&p,1);if((p.Buttons&~old)&PSP_CTRL_CIRCLE)return;old=p.Buttons;keep_awake();sceKernelDelayThread(20000);}
    }
    static const char *axis_names[]={"X","Y","Z","Rx","Ry","Rz"};
    unsigned selected=0,old=~0U;int dirty=1;
    input_bt_mapping_active=1;
    if(!sm_bt_setup.profile.configured)sm_bt_wizard();
    for(;;) {
        keep_awake();SceCtrlData p;sceCtrlReadBufferPositive(&p,1);unsigned pressed=p.Buttons&~old;
        if(dirty) {
            settings_shell(tr(TXT_SM_BT_OPTIONS));
            for(unsigned i=selected/7*7;i<17 && i<selected/7*7+7;i++) {
                char line[80],value[40];const char *name;
                if(!i){name=tr(TXT_SM_BT_WIZARD);strcpy(value,"[X]");}
                else if(i==1){name=tr(TXT_SM_BT_AUTO);snprintf(value,sizeof(value),"%s",tr(sm_bt_setup.reconnect?TXT_SETTINGS_ON:TXT_OFF));}
                else if(i<14){name=sm_bt_target_name(i-2);unsigned source=sm_bt_setup.profile.binding[i-2];
                    if(!source)strcpy(value,"-");else if(source<=16)snprintf(value,sizeof(value),"HID%02u",source);
                    else snprintf(value,sizeof(value),"D-pad %s",sm_bt_target_name(source-9));}
                else if(i==14){name=tr(TXT_SM_BT_ANALOG);
                    if(sm_bt_setup.profile.axis_x<6 && sm_bt_setup.profile.axis_y<6)snprintf(value,sizeof(value),"%s / %s",axis_names[sm_bt_setup.profile.axis_x],axis_names[sm_bt_setup.profile.axis_y]);
                    else strcpy(value,"-");}
                else {name=tr(i==15?TXT_SM_BT_INVERT_X:TXT_SM_BT_INVERT_Y);snprintf(value,sizeof(value),"%s",tr(sm_bt_setup.profile.invert&(i==15?1:2)?TXT_SETTINGS_ON:TXT_OFF));}
                snprintf(line,sizeof(line),"%c %s = %s",i==selected?'>':' ',name,value);settings_line(i%7,i==selected,line);
            }
            char device[32];snprintf(device,sizeof(device),"%02X:%02X:%02X:%02X:%02X:%02X",
                sm_bt_setup.address[0],sm_bt_setup.address[1],sm_bt_setup.address[2],sm_bt_setup.address[3],sm_bt_setup.address[4],sm_bt_setup.address[5]);
            settings_line(7,0,device);
            if(rc<0){char line[80];snprintf(line,sizeof(line),"%s: %08X",tr(TXT_SM_ERROR),(unsigned)rc);settings_line(8,0,line);}
            settings_help(tr(TXT_SM_BT_MAP_HELP));dirty=0;
        }
        if(pressed&PSP_CTRL_CIRCLE)break;
        if(pressed&PSP_CTRL_START){sm_bt_setup.profile.configured=1;rc=sm_run(SM_BT_SETUP_SET);dirty=1;if(rc>=0)break;}
        if(pressed&PSP_CTRL_UP){selected=(selected+16)%17;dirty=1;}
        if(pressed&PSP_CTRL_DOWN){selected=(selected+1)%17;dirty=1;}
        if(pressed&PSP_CTRL_CROSS) {
            if(!selected)sm_bt_wizard();
            else if(selected==1)sm_bt_setup.reconnect=!sm_bt_setup.reconnect;
            else if(selected<15)sm_bt_learn(selected-2);
            else sm_bt_setup.profile.invert^=selected==15?1:2;
            sceCtrlReadBufferPositive(&p,1); /* Do not leak capture's cancel into this menu. */
            dirty=1;
        }
        old=p.Buttons;sceKernelDelayThread(20000);
    }
    input_bt_mapping_active=0;
}
static void streammaster_bluetooth(void) {
    unsigned selected=0,old=PSP_CTRL_CROSS;int dirty=1,confirm=0;
    int rc=sm_run(SM_BT_STATUS);
    SceInt64 refresh_at=sceKernelGetSystemTimeWide()+2000000;
    int refreshing=0;
    uint8_t forget_address[6]={0};
    for(;;) {
        keep_awake();SceCtrlData p;sceCtrlReadBufferPositive(&p,1);unsigned pressed=p.Buttons&~old;
        if(refreshing && sm_finished && sm_reap()==0) {
            __sync_synchronize();rc=sm_result;
            if(rc>=0)sm_bt_status=sm_bt_refresh;
            refreshing=0;dirty=1;refresh_at=sceKernelGetSystemTimeWide()+2000000;
        }
        unsigned count=rc<0?3:3+sm_bt_status.count;if(selected>=count)selected=0;
        if(dirty) {
            settings_shell(tr(TXT_SM_BT));
            for(unsigned i=selected/7*7;i<count && i<selected/7*7+7;i++) {
                const char *name=i==0?tr(TXT_SM_BT_SCAN):i==1?tr(TXT_SM_BT_DISCONNECT):i==2?tr(TXT_SM_BT_OPTIONS):sm_bt_status.device[i-3].name;
                char line[80];snprintf(line,sizeof(line),"%c%c %.47s",i==selected?'>':' ',i>=3 && sm_bt_status.device[i-3].reserved?'*':' ',name);settings_line(i%7,i==selected,line);
            }
            static const TextId states[]={TXT_SM_BT_NONE,TXT_SM_BT_STARTING,TXT_SM_BT_READY,TXT_SM_BT_SCANNING,TXT_SM_BT_CONNECTING,TXT_SM_BT_CONNECTED,TXT_SM_BT_ERROR};
            char line[96];snprintf(line,sizeof(line),tr(TXT_SM_BT_STATE),(unsigned)sm_bt_status.reports,(unsigned)(rc<0?rc:sm_bt_status.error));
            settings_line(7,0,line);settings_line(8,0,confirm?tr(TXT_SM_BT_CONFIRM):tr(states[rc<0||sm_bt_status.state>SM_BT_ERROR?SM_BT_ERROR:sm_bt_status.state]));
            settings_help(tr(TXT_SM_BT_HELP));dirty=0;
        }
        if(pressed&PSP_CTRL_CIRCLE)break;
        if(refreshing){old=p.Buttons;sceKernelDelayThread(20000);continue;}
        if(pressed&PSP_CTRL_SQUARE){rc=sm_run(SM_BT_STATUS);dirty=1;confirm=0;}
        if((pressed&PSP_CTRL_TRIANGLE) && selected>=3 && selected<3+sm_bt_status.count) {
            memcpy(forget_address,sm_bt_status.device[selected-3].address,6);confirm=1;dirty=1;
        }
        if(pressed&PSP_CTRL_CROSS) {
            if(selected==2&&!confirm){streammaster_bt_options();dirty=1;}
            else {
                memset(&sm_bt_action,0,sizeof(sm_bt_action));
                sm_bt_action.action=confirm?SM_BT_FORGET:selected==0?SM_BT_SCAN:selected==1?SM_BT_DISCONNECT:SM_BT_PAIR;
                memcpy(sm_bt_action.address,confirm?forget_address:selected>=3?sm_bt_status.device[selected-3].address:sm_bt_status.selected,6);
                rc=sm_run(SM_BT_ACTION);confirm=0;dirty=1;
            }
        }
        if(pressed&(PSP_CTRL_UP|PSP_CTRL_DOWN)) {selected=(selected+(pressed&PSP_CTRL_UP?count-1:1))%count;confirm=0;dirty=1;}
        if(pressed)refresh_at=sceKernelGetSystemTimeWide()+2000000;
        if(!confirm && sceKernelGetSystemTimeWide()>=refresh_at && sm_reap()==0) {
            sm_running=1;sm_finished=0;
            sm_thread=sceKernelCreateThread("BT menu refresh",sm_bt_refresh_worker,0x18,16384,PSP_THREAD_ATTR_USER,NULL);
            if(sm_thread>=0) {
                if(sceKernelStartThread(sm_thread,0,NULL)>=0)refreshing=1;
                else {sceKernelDeleteThread(sm_thread);sm_thread=-1;}
            }
            refresh_at=sceKernelGetSystemTimeWide()+2000000;
        }
        old=p.Buttons;sceKernelDelayThread(20000);
    }
}
static void streammaster_settings(void) {
    enum {M_ATTACH,M_INFO,M_PROFILE,M_AUTOMATIC,M_SCAN,M_SSID,M_PASSWORD,M_DHCP,M_AUTO_DNS,M_IP,M_MASK,M_GATEWAY,M_DNS,M_DNS2,M_SAVE,M_CONNECT,M_DELETE,M_BENCH,M_SERVER,M_BT,M_COUNT};
    static const TextId labels[M_COUNT]={TXT_SM_ATTACH,TXT_SM_INFO,TXT_SM_PROFILE,TXT_SM_AUTOMATIC,TXT_SM_SCAN,TXT_SM_SSID,TXT_SM_PASSWORD,
        TXT_SM_DHCP,TXT_SM_AUTO_DNS,TXT_SM_IP,TXT_SM_MASK,TXT_SM_GATEWAY,TXT_SM_DNS,TXT_SM_DNS2,
        TXT_SM_SAVE,TXT_SM_CONNECT,TXT_SM_DELETE,TXT_SM_BENCH,TXT_SM_SERVER,TXT_SM_BT};
    int selected=0,dirty=1,rc=0,ready=0;char detail[80]="";
    unsigned old=PSP_CTRL_CROSS;unsigned long long repeat=0;
    int delete_confirm=0;
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
                if(i==M_PROFILE)snprintf(value,sizeof(value),"%u/5 %s",(unsigned)sm_slot+1,sm_draft.ssid[0]?"":tr(TXT_SM_EMPTY));
                if(i==M_AUTOMATIC)snprintf(value,sizeof(value),"%s",tr(sm_profiles_supported && sm_profiles.automatic?TXT_SETTINGS_ON:TXT_OFF));
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
            else if(selected==M_BT)streammaster_bluetooth();
            else if(selected==M_PROFILE) {
                if(!sm_profiles_supported)rc=SM_INVALID;
                else {sm_slot=(sm_slot+1)%SM_PROFILE_COUNT;sm_profile_draft();}
            } else if(selected==M_AUTOMATIC) {
                if(!sm_profiles_supported)rc=SM_INVALID;
                else {sm_profile_value=!sm_profiles.automatic;rc=sm_run(SM_PROFILE_AUTO);}
            } else if(selected==M_DELETE) {
                if(!sm_profiles_supported)rc=SM_INVALID;
                else if(!delete_confirm){delete_confirm=1;snprintf(detail,sizeof(detail),"%s",tr(TXT_SM_DELETE_CONFIRM));}
                else {sm_profile_value=sm_slot;rc=sm_run(SM_PROFILE_DELETE);delete_confirm=0;}
            }
            else if(selected==M_INFO) {
                rc=sm_run(SM_INFO);
                if(rc>=0)snprintf(detail,sizeof(detail),"%s / %s / %d dBm",tr((TextId)(TXT_SM_IDLE+sm_info.wifi_state%4)),sm_info.ip,(int)sm_info.rssi);
            } else if(selected==M_SCAN){rc=sm_run(SM_SCAN);if(rc>=0){if(sm_scan.count)sm_choose_ap();else snprintf(detail,sizeof(detail),"%s",tr(TXT_SM_NO_AP));}}
            else if(selected==M_DHCP){sm_draft.flags^=SM_CFG_DHCP;if(!(sm_draft.flags&SM_CFG_DHCP))sm_draft.flags&=~SM_CFG_AUTO_DNS;}
            else if(selected==M_AUTO_DNS){if(sm_draft.flags&SM_CFG_DHCP)sm_draft.flags^=SM_CFG_AUTO_DNS;else rc=SM_INVALID;}
            else if(selected==M_SAVE) {
                if(!sm_config_valid(&sm_draft,1))rc=SM_INVALID;
                else {rc=sm_run(sm_profiles_supported?SM_PROFILE_SAVE:SM_CONFIG_SET);if(rc>=0)snprintf(detail,sizeof(detail),"%s",tr(TXT_SM_SAVED));}
            } else if(selected==M_CONNECT){
                if(sm_profiles_supported && !sm_profiles.automatic){sm_profile_value=sm_slot;rc=sm_run(SM_PROFILE_SELECT);}
                else rc=sm_run(SM_CONNECT);
                memset(&sm_network,0,sizeof(sm_network));
            }
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
            delete_confirm=0;
            selected=(selected+(movement&PSP_CTRL_UP?M_COUNT-1:1))%M_COUNT;
            repeat=now+((pressed&movement)?400000:150000);dirty=1;
        }
        old=pad.Buttons;sceKernelDelayThread(20000);
    }
    streammaster_cleanup();
    if(sm_thread<0)memset(&sm_draft,0,sizeof(sm_draft));
}

/* SPDX-License-Identifier: MIT
 * Adapted PLL sequence from m-c/d's Experimental Overclock Stress Tester.
 * See LICENSE and README for source revision and safety limits.
 * No syscall/display hooks, permanent firmware patches or memory unlocking.
 */
#include <pspkernel.h>
#include <psppower.h>
#include <pspctrl.h>
#include <pspsdk.h>
#include <pspsysmem_kernel.h>
#include <pspinit.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspiofilemgr_kernel.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "clock_math.h"
#include "power_callback_slot.h"
#include "config_parse.h"
#include "report_io.h"
#include "overlay_pixels.h"
#include "control_api.h"

PSP_MODULE_INFO("StreamerOC", 0x1006, 1, 0);
PSP_NO_CREATE_MAIN_THREAD();
#define REG(a) (*(volatile unsigned int *)(a))
#define CTL REG(0xbc100068)
#define MUL REG(0xbc1000fc)
#define CPU REG(0xbc200000)
#define BUS REG(0xbc200004)
#define SYNC() __asm__ volatile("sync" ::: "memory")
static volatile int running, suspended;
static int worker=-1, enabled, target=333, enforce, enforce_unlimited, report=1, changed;
static int configured_enabled, power_callback_id=-1, power_slot=-1;
static int power_auto_result=-1, power_register_result=-1;
static int config_io_result, config_bytes, config_keys, config_error_line;
static const char *config_state="not attempted";
static char directory[192]="ms0:/SEPLUGINS/StreamerOC/";
static const char *status="monitor only";
static unsigned long long session_tick;
static char application[192];
static int journal_result;
static volatile int pending_suspend_flags;
static volatile unsigned long long suspend_tick;
static int overlay_enabled=1;
static int app_control=1, configured_target, sony_owned, control_registered;
static volatile int control_ready, control_pending=-1, control_result;
static unsigned long long overlay_until;
static unsigned long long overlay_next_draw;
static OcOverlay overlay;
#include "overlay_vblank.h"

/* The I/O manager dispatches this optional API without mandatory client
 * imports. All hardware writes stay in our worker, never the caller thread. */
static int control_devctl(PspIoDrvFileArg *arg,const char *name,unsigned int cmd,
                         void *in,int inlen,void *out,int outlen) {
    (void)arg;(void)name;
    if(in || out || inlen || outlen)return -1;
    if(cmd==OC_CMD_CPU_KHZ) {
        unsigned int khz=0;
        if(oc_supported_model(sceKernelGetModel())) {
            int intr=sceKernelCpuSuspendIntr();khz=oc_khz(CTL,MUL,CPU);sceKernelCpuResumeIntr(intr);
        }
        return khz?(int)khz:scePowerGetCpuClockFrequencyInt()*1000;
    }
    if(cmd==OC_CMD_TARGET)return target;
    if(!control_ready || !app_control || !enabled || suspended || !running)return -1;
    if(cmd==OC_CMD_STATUS)return control_pending>=0?1:control_result;
    if((cmd&OC_CMD_SET_MASK)!=OC_CMD_SET)return -1;
    int requested=(int)(cmd&0xfff);
    if(requested && !oc_target_valid(requested))return -1;
    /* App permission does not authorize a higher, untested overclock. */
    if(requested>333 && requested>configured_target)return -1;
    int intr=sceKernelCpuSuspendIntr();
    control_pending=requested;control_result=0;
    sceKernelCpuResumeIntr(intr);
    return 0;
}
static int control_init(PspIoDrvArg *arg){(void)arg;return 0;}
static PspIoDrvFuncs control_functions={.IoInit=control_init,.IoExit=control_init,.IoDevctl=control_devctl};
static PspIoDrv control_driver={"streameroc",0x10,0x800,"StreamerOC control",&control_functions};

static void overlay_notify(void) {
    if(overlay_enabled&&!suspended) {overlay_until=sceKernelGetSystemTimeWide()+5000000ULL;overlay_next_draw=0;}
}

static void overlay_update(int toggle) {
    if(!overlay_enabled)return;
    /* A resumed application may have reused VRAM: never restore stale pixels. */
    if(suspended){overlay.valid=0;overlay_until=0;return;}
    unsigned long long now=sceKernelGetSystemTimeWide();
    if(toggle)overlay_until=overlay_until?0:now+5000000ULL;
    if(overlay_until && now>=overlay_until)overlay_until=0;
    if(!overlay_until && !overlay.valid)return;
    if(overlay_until && now<overlay_next_draw && !toggle)return;
    /* Bounded polling, not an unbounded VBlank wait: display shutdown or
     * cable removal must not strand module_stop. Only active OSD waits. */
    if(!oc_overlay_vblank())return;
    if(suspended){overlay.valid=0;overlay_until=0;return;}
    void *base=NULL;int stride,format,mode,width,height;
    if(sceDisplayGetFrameBuf(&base,&stride,&format,PSP_DISPLAY_SETBUF_IMMEDIATE)<0 ||
       sceDisplayGetMode(&mode,&width,&height)<0 ||
       !oc_osd_layout((uintptr_t)base,sceGeEdramGetSize(),width,height,stride,format)) {
        overlay.valid=0;return;
    }
    if(overlay.valid && (overlay.stride!=stride || overlay.format!=format))overlay.valid=0;
    oc_osd_restore(&overlay);
    if(!overlay_until)return;
    unsigned int cpu=0,bus=0;
    if(oc_supported_model(sceKernelGetModel())) {
        int intr=sceKernelCpuSuspendIntr();
        unsigned int ctl=CTL,mul=MUL,c=CPU,b=BUS;
        sceKernelCpuResumeIntr(intr);
        cpu=oc_khz(ctl,mul,c);bus=oc_khz(ctl,mul,b);
    }
    char lines[3][40]={{0}};
    if(!cpu)cpu=scePowerGetCpuClockFrequencyInt()*1000;
    if(!bus)bus=scePowerGetBusClockFrequencyInt()*1000;
    snprintf(lines[0],40,"OC CPU %u.%u BUS %u.%u MHZ EST",cpu/1000,(cpu%1000)/100,bus/1000,(bus%1000)/100);
    snprintf(lines[1],40,"TARGET %d SONY %d MHZ",target,scePowerGetCpuClockFrequencyInt());
    snprintf(lines[2],40,"%s ENFORCE %s CB %s",enabled?"ACTIVE":"MONITOR",enforce?"ON":"OFF",power_slot>=0?"OK":"FAIL");
    oc_osd_draw(&overlay,(void *)(((uintptr_t)base&0x1fffffffU)|0x40000000U),stride,format,lines);
    overlay_next_draw=sceKernelGetSystemTimeWide()+33333ULL;
}

/* Same pipeline-settle loop as the reference, but every ready wait is bounded. */
static void settle(void) {
    __asm__ volatile(".set push\n.set noreorder\n"
        "lui $t0,0x02\nori $t0,$t0,0xffff\n"
        "1: nop\nnop\nnop\nnop\nnop\nnop\nnop\n"
        "addiu $t0,$t0,-1\nbnez $t0,1b\nnop\n.set pop\n" ::: "t0","memory");
}
static int ready(void) {
    for(unsigned int i=0;i<100000;i++) {if(!(CTL&0x80))return 0; SYNC();}
    return -1;
}
static void multiplier(unsigned int n) {MUL=(MUL&0xffff0000)|(n<<8)|OC_DEN;SYNC();}
static void config(void) {
    char path[256],buffer[1024];
    snprintf(path,sizeof(path),"%sStreamerOC.ini",directory);
    SceUID fd=sceIoOpen(path,PSP_O_RDONLY,0);
    config_io_result=fd;
    if(fd<0){config_state="open failed";status="config open failed: monitor only";return;}
    while(config_bytes<(int)sizeof(buffer)-1) {
        int n=sceIoRead(fd,buffer+config_bytes,sizeof(buffer)-1-config_bytes);
        config_io_result=n;
        if(n<=0)break;
        config_bytes+=n;
    }
    int closed=sceIoClose(fd);
    if(config_io_result<0){config_state="read failed";status="config read failed: monitor only";return;}
    if(closed<0){config_io_result=closed;config_state="close failed";status="config close failed: monitor only";return;}
    if(!config_bytes || config_bytes==(int)sizeof(buffer)-1){config_state="empty or oversized";status="invalid config: monitor only";return;}
    buffer[config_bytes]=0;
    OcConfig parsed;
    if(oc_config_parse(buffer,config_bytes,&parsed,&config_keys,&config_error_line)<0) {
        config_state="parse failed";status="invalid config: monitor only";return;
    }
    enabled=parsed.enabled;target=parsed.target;enforce=parsed.enforce;report=parsed.report;
    app_control=parsed.app_control;
    enforce_unlimited=parsed.enforce_unlimited;
    overlay_enabled=parsed.overlay;
    config_state="loaded";
}
static void snapshot(const char *event) {
    if(!report || suspended==1)return;
    unsigned int ctl=0,mul=0,cpu=0,bus=0;
    if(oc_supported_model(sceKernelGetModel())) {
        int intr=sceKernelCpuSuspendIntr();
        ctl=CTL;mul=MUL;cpu=CPU;bus=BUS;
        sceKernelCpuResumeIntr(intr);
    }
    char path[256],text[2048];
    unsigned long long now=sceKernelGetSystemTimeWide();
    int n=snprintf(text,sizeof(text),
        "\n[event=%s session_us=%u%06u worker=%d elapsed_ms=%u]\napplication=%s\n"
        "status=%s\nmodel=%d\nenabled=%d\nenforce=%d\nenforce_unlimited=%d\ntarget_mhz=%d\n"
        "app_control=%d\nconfigured_target_mhz=%d\ncontrol_driver=%d\ncontrol_result=%d\n"
        "config_path=%sStreamerOC.ini\nconfig_state=%s\nconfig_io_result=%08X\n"
        "config_bytes=%d\nconfig_keys=%d\nconfig_error_line=%d\n"
        "configured_enabled=%d\npower_callback_id=%08X\npower_callback_ready=%d\n"
        "power_callback_slot=%d\npower_auto_result=%08X\npower_register_result=%08X\n"
        "sony_api_mhz=%d\npll_estimate_khz=%u\ncpu_estimate_khz=%u\nbus_estimate_khz=%u\n"
        "pll_control=%08X\npll_multiplier=%08X\ncpu_domain=%08X\nbus_domain=%08X\n"
        "suspend_flags=%08X\nsuspend_observed_us=%u%06u\nprevious_journal_result=%08X\noverlay=%d\n"
        "Estimates assume the reference 37 MHz base and PLL ratio index 5.\n"
        "Zero means unknown/unsupported, not zero MHz. Not a speed or stability measurement.\n",
        event,(unsigned int)(session_tick/1000000ULL),(unsigned int)(session_tick%1000000ULL),worker,
        (unsigned int)((now-session_tick)/1000ULL),application,
        status,sceKernelGetModel(),enabled,enforce,enforce_unlimited,target,
        app_control,configured_target,control_registered,control_result,
        directory,config_state,(unsigned int)config_io_result,config_bytes,config_keys,config_error_line,
        configured_enabled,(unsigned int)power_callback_id,power_slot>=0,power_slot,
        (unsigned int)power_auto_result,(unsigned int)power_register_result,
        scePowerGetCpuClockFrequencyInt(),
        oc_khz(ctl,mul,0x01ff01ff),oc_khz(ctl,mul,cpu),oc_khz(ctl,mul,bus),ctl,mul,cpu,bus,
        (unsigned int)pending_suspend_flags,(unsigned int)(suspend_tick/1000000ULL),
        (unsigned int)(suspend_tick%1000000ULL),(unsigned int)journal_result,overlay_enabled);
    if(n<0)return;
    if(n>=(int)sizeof(text))n=sizeof(text)-1;
    snprintf(path,sizeof(path),"%sStreamerOC-events.log",directory);
    journal_result=oc_report_write(path,text,n,1);
    snprintf(path,sizeof(path),"%sStreamerOC-status.txt",directory);
    oc_report_write(path,text,n,0);
}
static int matches(void);
static void restore(void);
static int apply(void) {
    if(!running || suspended)return -2;
    if(target<=333) {
        if(changed)restore();
        if(scePowerSetClockFrequency(333,target,target/2)<0)return -1;
        sony_owned=target;changed=1;
        return matches()?0:-3;
    }
    if(sony_owned)restore();
    /* The tester starts from Sony's 333/333/166 setup, then adjusts PLL and
     * domain ratios. Its busy loops and unbounded upward scan are not copied. */
    if(scePowerSetClockFrequency(333,333,166)<0)return -1;
    changed=1;
    int intr=sceKernelCpuSuspendIntr();
    multiplier(OC_NORMAL_NUM);settle();
    if((CTL&15)!=5){CTL=(CTL&0xffffff00)|0x85;SYNC();}
    int result=ready();
    if(!result) {
        unsigned int cn=(CPU>>16)&511,cd=CPU&511,bn=(BUS>>16)&511,bd=BUS&511;
        /* Reference: add 18 to each divider component until all reach 511. */
        while(cn!=511 || cd!=511 || bn!=511 || bd!=511) {
            cn=cn+18>511?511:cn+18;cd=cd+18>511?511:cd+18;
            bn=bn+18>511?511:bn+18;bd=bd+18>511?511:bd+18;
            CPU=(cn<<16)|cd;BUS=(bn<<16)|bd;SYNC();settle();
        }
    }
    sceKernelCpuResumeIntr(intr);
    if(result)return result;
    unsigned int wanted=oc_numerator(target);
    for(unsigned int num=OC_NORMAL_NUM+1;num<=wanted;num++) {
        if(!running || suspended)return -2;
        intr=sceKernelCpuSuspendIntr();
        /* Another thread may have changed clocks during the previous yield.
         * Never continue a ramp against an unknown PLL/domain configuration. */
        if((CTL&0x8f)!=5 || (MUL&0xffff)!=(((num-1)<<8)|OC_DEN) ||
           (CPU&0x01ff01ff)!=0x01ff01ff || (BUS&0x01ff01ff)!=0x01ff01ff) {
            sceKernelCpuResumeIntr(intr);return -3;
        }
        multiplier(num);settle();sceKernelCpuResumeIntr(intr);
        sceKernelDelayThreadCB(10000);
    }
    return matches()?0:-3;
}
static int matches(void) {
    if(target<=333) {
        /* Sony still reports 333 after a direct PLL overclock. Never treat
         * that cached value as proof that switching back already happened. */
        if(sony_owned!=target)return 0;
        unsigned int cpu=oc_khz(CTL,MUL,CPU),bus=oc_khz(CTL,MUL,BUS);
        return abs(scePowerGetCpuClockFrequencyInt()-target)<=1 &&
            abs(scePowerGetBusClockFrequencyInt()-target/2)<=1 &&
            (!cpu || abs((int)cpu-target*1000)<=2000) &&
            (!bus || abs((int)bus-(target/2)*1000)<=2000);
    }
    return (CTL&0x8f)==5 && (MUL&0xffff)==((oc_numerator(target)<<8)|OC_DEN) &&
        (CPU&0x01ff01ff)==0x01ff01ff && (BUS&0x01ff01ff)==0x01ff01ff;
}
static void restore(void) {
    if(sony_owned) {
        if(abs(scePowerGetCpuClockFrequencyInt()-sony_owned)<=1 &&
           abs(scePowerGetBusClockFrequencyInt()-sony_owned/2)<=1)
            scePowerSetClockFrequency(333,333,166);
        sony_owned=0;return;
    }
    /* Only undo our known register recipe, not a different plugin's PLL.
     * Start at the actual numerator, not the configured maximum (tester bug). */
    if((CTL&0x8f)!=5 || (MUL&255)!=OC_DEN)return;
    unsigned int num=(MUL>>8)&255;
    while(num>OC_NORMAL_NUM) {
        int intr=sceKernelCpuSuspendIntr();
        if((CTL&0x8f)!=5 || (MUL&0xffff)!=((num<<8)|OC_DEN)) {
            sceKernelCpuResumeIntr(intr);return;
        }
        multiplier(--num);settle();sceKernelCpuResumeIntr(intr);
    }
    scePowerSetClockFrequency(333,333,166);
}
static int power_callback(int count,int flags,void *arg) {
    (void)count;(void)arg;
    if(flags&(PSP_POWER_CB_SUSPENDING|PSP_POWER_CB_STANDBY)) {
        pending_suspend_flags|=flags;
        suspend_tick=sceKernelGetSystemTimeWide();
        suspended=1;
    }
    if(flags&PSP_POWER_CB_RESUME_COMPLETE)suspended=2;
    return 0;
}
static int thread_main(SceSize args,void *argp) {
    (void)args;(void)argp;
    session_tick=sceKernelGetSystemTimeWide();
    const char *filename=sceKernelInitFileName();
    snprintf(application,sizeof(application),"%s",filename?filename:"unknown");
    config();
    configured_target=target;
    configured_enabled=enabled;
    int model=sceKernelGetModel();
    if(!oc_supported_model(model)) {enabled=0;status="unsupported model: no register writes";}
    if(sceKernelInitKeyConfig()!=PSP_INIT_KEYCONFIG_GAME){enabled=0;status="not GAME context: monitor only";}
    power_callback_id=sceKernelCreateCallback("StreamerOC power",power_callback,NULL);
    if(power_callback_id>=0)
        power_slot=oc_register_power_callback(power_callback_id,&power_auto_result,&power_register_result);
    if(power_slot<0){enabled=0;status="power callback unavailable: monitor only";}
    snapshot("session_start");
    SceInt64 start_until=sceKernelGetSystemTimeWide()+6000000LL;
    while(running && sceKernelGetSystemTimeWide()<start_until)sceKernelDelayThreadCB(100000);
    SceCtrlData pad;sceCtrlPeekBufferPositive(&pad,1);
    if(pad.Buttons&PSP_CTRL_RTRIGGER){enabled=0;status="R bypass: monitor only";}
    if(running && enabled && !suspended && sceKernelInitKeyConfig()==PSP_INIT_KEYCONFIG_GAME) {
        int r=apply();status=r?"PLL apply failed: enforcement disabled":"target applied";if(r)enabled=0;
    }
    snapshot("startup_result");
    control_ready=1;
    if(enabled)overlay_notify();
    unsigned int previous=0,conflicts=0;
    unsigned int last_ctl=0,last_mul=0,last_cpu=0,last_bus=0;
    int last_sony=-1;
    unsigned long long window=sceKernelGetSystemTimeWide();
    unsigned long long next_clock_check=0;
    while(running) {
        sceKernelDelayThreadCB(overlay_until?16666:overlay_enabled?100000:500000);
        if(!running)break;
        if(suspended==1){overlay_update(0);continue;}
        if(sceKernelInitKeyConfig()!=PSP_INIT_KEYCONFIG_GAME) {
            enabled=0;status="left GAME context: enforcement disabled";
            snapshot("left_game_before_restore");
            if(changed){restore();changed=0;}
            snapshot("left_game_after_restore");break;
        }
        if(suspended==2) {
            overlay_update(0);
            /* Suspend is a safety boundary: do not silently re-overclock. */
            suspended=0;enabled=0;status="resumed: enforcement disabled; restart to enable";
            snapshot("suspend_resume");pending_suspend_flags=0;
            control_pending=-1;
        }
        if(control_pending>=0 && enabled && !suspended) {
            /* Keep pending set while applying so clients can wait for ack. */
            int request=control_pending;
            target=request?request:configured_target;
            int r=matches()?0:apply();
            control_result=r;
            if(r)enabled=0;
            status=r?"app clock request failed: disabled":"app clock request applied";
            int intr=sceKernelCpuSuspendIntr();
            if(control_pending==request)control_pending=-1;
            sceKernelCpuResumeIntr(intr);
            conflicts=0;window=sceKernelGetSystemTimeWide();
            snapshot("app_clock_result");overlay_notify();
        }
        sceCtrlPeekBufferPositive(&pad,1);
        unsigned int buttons=pad.Buttons;
        unsigned int osd_chord=PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER|PSP_CTRL_TRIANGLE;
        overlay_update((buttons&osd_chord)==osd_chord && (previous&osd_chord)!=osd_chord);
        if((buttons&(PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER|PSP_CTRL_SELECT))==
           (PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER|PSP_CTRL_SELECT) &&
           (previous&(PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER|PSP_CTRL_SELECT))!=
           (PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER|PSP_CTRL_SELECT)) snapshot("manual_snapshot");
        previous=buttons;
        unsigned long long clock_tick=sceKernelGetSystemTimeWide();
        if(clock_tick<next_clock_check)continue;
        next_clock_check=clock_tick+500000ULL;
        if(oc_supported_model(model)) {
            int intr=sceKernelCpuSuspendIntr();
            unsigned int ctl=CTL,mul=MUL,cpu=CPU,bus=BUS;
            sceKernelCpuResumeIntr(intr);
            int sony=scePowerGetCpuClockFrequencyInt();
            if(last_sony>=0 && (ctl!=last_ctl || mul!=last_mul || cpu!=last_cpu || bus!=last_bus || sony!=last_sony)) {
                snapshot("clock_observed_change");overlay_notify();
            }
            last_ctl=ctl;last_mul=mul;last_cpu=cpu;last_bus=bus;last_sony=sony;
        }
        if(!enabled || matches())continue;
        snapshot("clock_mismatch");
        if(!enforce){enabled=0;status="application changed clocks: not reapplied";snapshot("enforcement_disabled");continue;}
        unsigned long long now=sceKernelGetSystemTimeWide();
        if(now-window>=60000000ULL){conflicts=0;window=now;}
        if(!enforce_unlimited && ++conflicts>3){enabled=0;status="clock conflict: enforcement disabled";snapshot("conflict_limit");continue;}
        int r=apply();status=r?"reapply failed: disabled":"target reapplied";if(r)enabled=0;
        snapshot("reapply_result");
    }
    snapshot("session_stopping");
    control_ready=0;
    overlay_until=0;overlay_update(0);
    if(changed && !suspended)restore();
    enabled=0;status="worker stopped";
    snapshot("session_end");
    if(power_slot>=0)scePowerUnregisterCallback(power_slot);
    if(power_callback_id>=0)sceKernelDeleteCallback(power_callback_id);
    return 0;
}
int module_start(SceSize args,void *argp) {
    if(args && argp) {
        /* CFW passes the plugin pathname. Use bounded data, never assume NUL. */
        unsigned int n=args<sizeof(directory)?args:sizeof(directory)-1;
        char path[192];memcpy(path,argp,n);path[n]=0;
        char *slash=strrchr(path,'/');
        if(slash){slash[1]=0;snprintf(directory,sizeof(directory),"%s",path);}
    }
    running=1;
    control_registered=sceIoAddDrv(&control_driver)>=0;
    worker=sceKernelCreateThread("StreamerOC",thread_main,0x30,0x4000,0,NULL);
    if(worker<0){running=0;if(control_registered)sceIoDelDrv("streameroc");return worker;}
    int r=sceKernelStartThread(worker,0,NULL);
    if(r<0){running=0;sceKernelDeleteThread(worker);worker=-1;if(control_registered)sceIoDelDrv("streameroc");}
    return r;
}
int module_stop(SceSize args,void *argp) {
    (void)args;(void)argp;running=0;
    control_ready=0;
    if(control_registered){sceIoDelDrv("streameroc");control_registered=0;}
    if(worker>=0){sceKernelWaitThreadEnd(worker,NULL);sceKernelDeleteThread(worker);worker=-1;}
    return 0;
}

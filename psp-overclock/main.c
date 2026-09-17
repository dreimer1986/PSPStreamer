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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "clock_math.h"
#include "power_callback_slot.h"

PSP_MODULE_INFO("StreamerOC", 0x1006, 1, 0);
PSP_NO_CREATE_MAIN_THREAD();
#define REG(a) (*(volatile unsigned int *)(a))
#define CTL REG(0xbc100068)
#define MUL REG(0xbc1000fc)
#define CPU REG(0xbc200000)
#define BUS REG(0xbc200004)
#define SYNC() __asm__ volatile("sync" ::: "memory")
static volatile int running, suspended;
static int worker=-1, enabled, target=333, enforce, report=1, changed;
static int configured_enabled, power_callback_id=-1, power_slot=-1;
static int power_auto_result=-1, power_register_result=-1;
static char directory[192]="ms0:/SEPLUGINS/StreamerOC/";
static const char *status="monitor only";

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
    if(fd<0)return;
    int length=sceIoRead(fd,buffer,sizeof(buffer)-1);sceIoClose(fd);
    if(length<=0 || length==(int)sizeof(buffer)-1){status="invalid config";return;}
    buffer[length]=0;
    char *line=strtok(buffer,"\r\n");
    while(line) {
        char *equal=strchr(line,'=');
        if(equal && line[0]!='#') {
            *equal=0;char *end;long value=strtol(equal+1,&end,10);
            if(end==equal+1 || *end || value<0 || value>471) {enabled=0;status="invalid config: disabled";return;}
            if(!strcmp(line,"enabled"))enabled=value==1;
            else if(!strcmp(line,"target_mhz"))target=value;
            else if(!strcmp(line,"enforce"))enforce=value==1;
            else if(!strcmp(line,"report"))report=value==1;
        }
        line=strtok(NULL,"\r\n");
    }
    if(target<333 || target>471){enabled=0;status="invalid target: disabled";}
}
static void snapshot(void) {
    if(!report || suspended==1)return;
    unsigned int ctl=0,mul=0,cpu=0,bus=0;
    if(oc_supported_model(sceKernelGetModel())) {
        int intr=sceKernelCpuSuspendIntr();
        ctl=CTL;mul=MUL;cpu=CPU;bus=BUS;
        sceKernelCpuResumeIntr(intr);
    }
    char path[256],text[1024];
    snprintf(path,sizeof(path),"%sStreamerOC-status.txt",directory);
    int n=snprintf(text,sizeof(text),
        "status=%s\nmodel=%d\nenabled=%d\nenforce=%d\ntarget_mhz=%d\n"
        "configured_enabled=%d\npower_callback_id=%08X\npower_callback_ready=%d\n"
        "power_callback_slot=%d\npower_auto_result=%08X\npower_register_result=%08X\n"
        "sony_api_mhz=%d\npll_estimate_khz=%u\ncpu_estimate_khz=%u\nbus_estimate_khz=%u\n"
        "pll_control=%08X\npll_multiplier=%08X\ncpu_domain=%08X\nbus_domain=%08X\n"
        "Estimates assume the reference 37 MHz base and PLL ratio index 5.\n"
        "Zero means unknown/unsupported, not zero MHz. Not a speed or stability measurement.\n",
        status,sceKernelGetModel(),enabled,enforce,target,
        configured_enabled,(unsigned int)power_callback_id,power_slot>=0,power_slot,
        (unsigned int)power_auto_result,(unsigned int)power_register_result,
        scePowerGetCpuClockFrequencyInt(),
        oc_khz(ctl,mul,0x01ff01ff),oc_khz(ctl,mul,cpu),oc_khz(ctl,mul,bus),ctl,mul,cpu,bus);
    if(n<0)return;
    if(n>=(int)sizeof(text))n=sizeof(text)-1;
    SceUID fd=sceIoOpen(path,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0600);
    if(fd>=0){sceIoWrite(fd,text,n);sceIoClose(fd);}
}
static int matches(void);
static int apply(void) {
    if(!running || suspended)return -2;
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
    return (CTL&0x8f)==5 && (MUL&0xffff)==((oc_numerator(target)<<8)|OC_DEN) &&
        (CPU&0x01ff01ff)==0x01ff01ff && (BUS&0x01ff01ff)==0x01ff01ff;
}
static void restore(void) {
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
    if(flags&(PSP_POWER_CB_SUSPENDING|PSP_POWER_CB_STANDBY))suspended=1;
    if(flags&PSP_POWER_CB_RESUME_COMPLETE)suspended=2;
    return 0;
}
static int thread_main(SceSize args,void *argp) {
    (void)args;(void)argp;
    config();
    configured_enabled=enabled;
    int model=sceKernelGetModel();
    if(!oc_supported_model(model)) {enabled=0;status="unsupported model: no register writes";}
    if(sceKernelInitKeyConfig()!=PSP_INIT_KEYCONFIG_GAME){enabled=0;status="not GAME context: monitor only";}
    power_callback_id=sceKernelCreateCallback("StreamerOC power",power_callback,NULL);
    if(power_callback_id>=0)
        power_slot=oc_register_power_callback(power_callback_id,&power_auto_result,&power_register_result);
    if(power_slot<0){enabled=0;status="power callback unavailable: monitor only";}
    SceInt64 start_until=sceKernelGetSystemTimeWide()+6000000LL;
    while(running && sceKernelGetSystemTimeWide()<start_until)sceKernelDelayThreadCB(100000);
    SceCtrlData pad;sceCtrlPeekBufferPositive(&pad,1);
    if(pad.Buttons&PSP_CTRL_RTRIGGER){enabled=0;status="R bypass: monitor only";}
    if(running && enabled && !suspended && sceKernelInitKeyConfig()==PSP_INIT_KEYCONFIG_GAME) {
        int r=apply();status=r?"PLL apply failed: enforcement disabled":"target applied";if(r)enabled=0;
    }
    snapshot();
    unsigned int previous=0,conflicts=0;
    unsigned long long window=sceKernelGetSystemTimeWide();
    while(running) {
        sceKernelDelayThreadCB(500000);
        if(suspended==1)continue;
        if(sceKernelInitKeyConfig()!=PSP_INIT_KEYCONFIG_GAME) {
            enabled=0;status="left GAME context: enforcement disabled";
            if(changed){restore();changed=0;}
            snapshot();break;
        }
        if(suspended==2) {
            /* Suspend is a safety boundary: do not silently re-overclock. */
            suspended=0;enabled=0;status="resumed: enforcement disabled; restart to enable";snapshot();
        }
        sceCtrlPeekBufferPositive(&pad,1);
        unsigned int buttons=pad.Buttons;
        if((buttons&(PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER|PSP_CTRL_SELECT))==
           (PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER|PSP_CTRL_SELECT) && buttons!=previous) snapshot();
        previous=buttons;
        if(!enabled || matches())continue;
        if(!enforce){enabled=0;status="application changed clocks: not reapplied";snapshot();continue;}
        unsigned long long now=sceKernelGetSystemTimeWide();
        if(now-window>=60000000ULL){conflicts=0;window=now;}
        if(++conflicts>3){enabled=0;status="clock conflict: enforcement disabled";snapshot();continue;}
        int r=apply();status=r?"reapply failed: disabled":"target reapplied";if(r)enabled=0;
        snapshot();
    }
    if(changed && !suspended)restore();
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
    worker=sceKernelCreateThread("StreamerOC",thread_main,0x30,0x4000,0,NULL);
    if(worker<0){running=0;return worker;}
    int r=sceKernelStartThread(worker,0,NULL);
    if(r<0){running=0;sceKernelDeleteThread(worker);worker=-1;}
    return r;
}
int module_stop(SceSize args,void *argp) {
    (void)args;(void)argp;running=0;
    if(worker>=0){sceKernelWaitThreadEnd(worker,NULL);sceKernelDeleteThread(worker);worker=-1;}
    return 0;
}

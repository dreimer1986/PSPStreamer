/* SPDX-License-Identifier: GPL-2.0-or-later
 * Keep FTP's parser, transport imports and service stack out of early VSH startup.
 * No filesystem access or USB calls on the module-manager start thread. */
#ifndef PSP_FTP_LOADER_HOST_TEST
#include <pspkernel.h>
#include <pspinit.h>
#include <psploadcore.h>
#include <pspsysmem_kernel.h>
#endif
#include <stdio.h>
#include "startup.h"
PSP_MODULE_INFO("PSPStreamFTP",PSP_MODULE_KERNEL,0,6);
static volatile int running;
static SceUID thread=-1,service=-1;
#define FTP_BASE "ms0:/SEPLUGINS/PSPStreamFTP/"
static void record(const char *phase,int rc) {
    char text[160];unsigned long long now=sceKernelGetSystemTimeWide();
    int n=snprintf(text,sizeof(text),"%08X%08X us FTP loader 0.6 %s: %08X\n",
        (unsigned)(now>>32),(unsigned)now,phase,(unsigned)rc);
    int fd=sceIoOpen(FTP_BASE "loader.log",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0600);
    if(fd>=0){if(n>0 && n<(int)sizeof(text))sceIoWrite(fd,text,n);sceIoClose(fd);}
}
static int worker(SceSize size,void *args) {
    (void)size;(void)args;
    FtpStartup state={0};
    unsigned long long begin=sceKernelGetSystemTimeWide();
    while(running) {
        unsigned long long now=sceKernelGetSystemTimeWide();
        int ready=now-begin>=20000000ULL && sceKernelGetSystemStatus()==0x20000 &&
            sceKernelFindModuleByName("vsh_module") && sceKernelFindModuleByName("scePaf_Module");
        if(ftp_startup_ready(&state,now,ready))break;
        sceKernelDelayThread(100000);
    }
    if(!running)return 0;
    /* First I/O occurs only after shell readiness, never during module_start. */
    record("ready; kernel free",sceKernelPartitionTotalFreeMemSize(1));
    record("kernel largest",sceKernelPartitionMaxFreeMemSize(1));
    if(sceKernelFindModuleByName("PSPStreamFTPService")){
        record("service already loaded; not taking ownership",0);return 0;
    }
    int id=sceKernelLoadModule(FTP_BASE "PSPStreamFTPService.prx",0,NULL);
    record("service load",id);
    if(id<0)return 0; /* A missing/rejected service must leave VSH running. */
    service=id;
    if(!running){if(sceKernelUnloadModule(service)>=0)service=-1;return 0;}
    int status=0,rc=sceKernelStartModule(service,0,NULL,&status,NULL);
    record("service start",rc);record("service entry result",status);
    if(rc<0 || status<0){if(sceKernelUnloadModule(service)>=0)service=-1;}
    return 0;
}
int module_start(SceSize size,void *args) {
    (void)size;(void)args;
    if(sceKernelInitKeyConfig()!=PSP_INIT_KEYCONFIG_VSH)return 0;
    running=1;
    thread=sceKernelCreateThread("FTP delayed loader",worker,0x38,4096,0,NULL);
    if(thread<0){running=0;return 0;}
    int rc=sceKernelStartThread(thread,0,NULL);
    if(rc<0){sceKernelDeleteThread(thread);thread=-1;running=0;}
    return 0;
}
int module_stop(SceSize size,void *args) {
    (void)size;(void)args;running=0;
    if(thread>=0){SceUInt timeout=12000000;
        if(sceKernelWaitThreadEnd(thread,&timeout)<0)return -1;
        sceKernelDeleteThread(thread);thread=-1;
    }
    if(service>=0){int status=0;
        int rc=sceKernelStopModule(service,0,NULL,&status,NULL);
        if(rc<0 || status<0)return -1; /* Never unload live FTP code. */
        if(sceKernelUnloadModule(service)<0)return -1;
        service=-1;
    }
    return 0;
}

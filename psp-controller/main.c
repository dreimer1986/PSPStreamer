/* SPDX-License-Identifier: GPL-2.0-or-later
 * Loader only: load Sony USB before resolving the shared bridge imports. */
#include <pspkernel.h>
#include <psploadcore.h>
#include <stdio.h>
PSP_MODULE_INFO("PSPConsolizer",PSP_MODULE_KERNEL,0,2);
static SceUID worker=-1;
static void report(const char *what,int rc) {
    char line[120];int n=snprintf(line,sizeof(line),"%s: %08X\n",what,(unsigned)rc);
    int fd=sceIoOpen("ms0:/SEPLUGINS/StreamMasterPad/loader.log",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0666);
    if(fd>=0){sceIoWrite(fd,line,n);sceIoClose(fd);}
}
static int load_start(const char *path) {
    int m=sceKernelLoadModule(path,0,NULL),status=0;
    if(m<0)return m;
    int rc=sceKernelStartModule(m,0,NULL,&status,NULL);
    if(rc<0 || status<0){sceKernelUnloadModule(m);return rc<0?rc:status;}
    return 0;
}
static int start_worker(SceSize size,void *args) {
    (void)size;(void)args;sceKernelDelayThread(2000000);
    const char *logs[]={"ms0:/SEPLUGINS/StreamMasterPad/loader.log","ms0:/SEPLUGINS/StreamMasterPad/last.log"};
    for(unsigned i=0;i<2;i++){int f=sceIoOpen(logs[i],PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0666);if(f>=0)sceIoClose(f);}
    int rc=0;
    if(!sceKernelFindModuleByName("sceUSB_Driver")) {
        rc=load_start("flash0:/kd/usb.prx");
        if((unsigned)rc==0x80020139U)rc=0;
    }
    report("Sony USB",rc);
    if(rc>=0 && !sceKernelFindModuleByName("StreamMasterUSB"))
        rc=load_start("ms0:/SEPLUGINS/StreamMasterPad/StreamMasterUSB.prx");
    report("shared bridge",rc);return 0;
}
int module_start(SceSize size,void *args) {
    (void)size;(void)args;
    worker=sceKernelCreateThread("StreamMasterPad loader",start_worker,0x30,4096,0,NULL);
    if(worker<0)return worker;
    int rc=sceKernelStartThread(worker,0,NULL);
    if(rc<0){sceKernelDeleteThread(worker);worker=-1;}return rc;
}
int module_stop(SceSize size,void *args) {
    (void)size;(void)args;
    if(worker>=0){SceUInt wait=100000;if(sceKernelWaitThreadEnd(worker,&wait)<0)return -1;sceKernelDeleteThread(worker);worker=-1;}
    return 0;
}

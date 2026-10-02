/* SPDX-License-Identifier: MIT
 * FuSa compatibility investigation. No display/register/code writes. */
#include <pspkernel.h>
#include <pspinit.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspsysmem_kernel.h>
#include <systemctrl.h>
#include <stdio.h>
#include <string.h>
PSP_MODULE_INFO("FuSaProbe",0x1006,0,1);
PSP_NO_CREATE_MAIN_THREAD();
static volatile int running;
static SceUID worker=-1;
static void log_write(const char *line,int n)
{
    if(n<=0)return;
    if(n>=384)n=383;
    int f=sceIoOpen("ms0:/SEPLUGINS/FuSaProbe/probe.log",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0666);
    if(f>=0){sceIoWrite(f,line,n);sceIoClose(f);}
}
#define log_line(...) do { char line[384]; int n=snprintf(line,sizeof(line),__VA_ARGS__); log_write(line,n); } while(0)
typedef struct { const char *module,*library;unsigned nid; } Export;
static const Export exports[]={
    {"sceGE_Manager","sceGe_driver",0x1F6752AD},
    {"sceGE_Manager","sceGe_driver",0x5BAA5439},
    {"sceDisplay_Service","sceDisplay",0x289D82FE},
    {"sceDisplay_Service","sceDisplay",0x0E20F177},
    {"sceDisplay_Service","sceDisplay",0xB4F378FA},
    {"sceDisplay_Service","sceDisplay_driver",0x63E22A26},
    {"sceDisplay_Service","sceDisplay_driver",0x5B5AEFAD},
    {"sceHP_Remote_Driver","sceHprm_driver",0x1528D408},
    {"sceDisplay_Service","sceHibari_driver",0x1F7D879D},
    {"sceImpose_Driver","sceImpose_driver",0x116DDED6},
    {"sceDisplay_Service","sceDve_driver",0xDEB2F80C},
    {"sceDisplay_Service","sceDve_driver",0x93828323},
    {"sceDisplay_Service","sceDve_driver",0x0B85524C},
    {"sceDisplay_Service","sceDve_driver",0xA265B504},
};
static int probe(SceSize size,void *args)
{
    (void)size;(void)args;
    const char *app=sceKernelInitFileName();
    log_line("\nFuSaProbe 0.1 fw=%08X model=%d context=%X app=%.192s\n",
        sceKernelDevkitVersion(),sceKernelGetModel(),sceKernelInitKeyConfig(),app?app:"");
    /* Allow the game and Sony TV transition to initialize. Bounded and cancellable. */
    for(int i=0;i<100&&running;i++)sceKernelDelayThread(100000);
    if(!running)return 0;
    for(unsigned i=0;i<sizeof(exports)/sizeof(exports[0]);i++) {
        const Export *e=&exports[i];
        unsigned address=sctrlHENFindFunction(e->module,e->library,e->nid);
        log_line("export %s %s %08X = %08X\n",e->module,e->library,e->nid,address);
    }
    log_line("edram_bytes=%u kernel_free=%u kernel_largest=%u\n",
        sceGeEdramGetSize(),sceKernelPartitionTotalFreeMemSize(1),sceKernelPartitionMaxFreeMemSize(1));
    for(int i=0;i<120&&running;i++) {
        int mode=-1,w=0,h=0,stride=0,format=-1;void *frame=NULL;
        int mr=sceDisplayGetMode(&mode,&w,&h);
        int fr=sceDisplayGetFrameBuf(&frame,&stride,&format,PSP_DISPLAY_SETBUF_NEXTFRAME);
        /* One small record per second, append/close so an exit preserves it. */
        log_line("sample=%d mode_rc=%08X mode=%X size=%dx%d frame_rc=%08X frame=%08X stride=%d format=%d\n",
            i,mr,mode,w,h,fr,(unsigned)frame,stride,format);
        for(int j=0;j<10&&running;j++)sceKernelDelayThread(100000);
    }
    log_line("probe finished; no hooks or display changes applied\n");
    return 0;
}
int module_start(SceSize size,void *args)
{
    (void)size;(void)args;
    if(sceKernelInitKeyConfig()!=PSP_INIT_KEYCONFIG_GAME)return 0;
    running=1;worker=sceKernelCreateThread("FuSa read-only probe",probe,0x38,4096,0,NULL);
    if(worker<0)return worker;
    int rc=sceKernelStartThread(worker,0,NULL);
    if(rc<0){running=0;sceKernelDeleteThread(worker);worker=-1;}
    return rc;
}
int module_stop(SceSize size,void *args)
{
    (void)size;(void)args;running=0;
    if(worker>=0){SceUInt wait=2000000;if(sceKernelWaitThreadEnd(worker,&wait)<0)return -1;
        sceKernelDeleteThread(worker);worker=-1;}
    return 0;
}

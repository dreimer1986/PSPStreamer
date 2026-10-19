/* SPDX-License-Identifier: GPL-2.0-or-later
 * Ask Sony's shell/impose handler to perform its normal display-button
 * transition. Do not impose PSPStreamer's 720px framebuffer on a game.
 * 0 off, 1 connected controller, 2 application start (also VSH/POPS). */
#include "../psp-client/tv_output_layout.h"
static int controller_tvout;
static unsigned long long controller_tv_hold_until;
static int (*controller_check_cable)(int *);
static unsigned controller_tv_button(unsigned long long now,int connected,int streamer,int permitted) {
    static unsigned long long ready_at,next_check,stable_since,external_since;
    static int attempted,ready_logged,last_stride=-1,last_format=-1;
    if(!ready_at)ready_at=now+8000000;
    if(!controller_tvout || !permitted || streamer || controller_suspended || controller_disabled){controller_tv_hold_until=0;external_since=0;return 0;}
    if(controller_tv_hold_until){
        int mode,width,height;
        int rc=sceDisplayGetMode(&mode,&width,&height);
        /* A transient mode-query failure is not a completed TV transition.
         * Require a stable external mode before releasing the long press. */
        if(rc>=0 && mode!=0){if(!external_since)external_since=now;}
        else external_since=0;
        if((external_since && now-external_since>=300000) || now>=controller_tv_hold_until){
            int confirmed=external_since && now-external_since>=300000;
            controller_tv_hold_until=0;external_since=0;
            controller_log(confirmed?"TV external mode confirmed":"TV long press expired without confirmation",rc<0?rc:mode);
            return 0;
        }
        return PSP_CTRL_SCREEN;
    }
    if(attempted || now<ready_at || now<next_check || (controller_tvout==1 && !connected))return 0;
    next_check=now+1000000;
    if(!tv_model_has_output(sceKernelGetModel())){attempted=1;controller_log("TV unavailable on this PSP model",0);return 0;}
    if(!controller_check_cable)controller_check_cable=(void *)sctrlHENFindFunction("sceImpose_Driver","sceImpose_driver",0x116CFF64);
    if(!controller_check_cable)return 0;
    int cable=0;if(controller_check_cable(&cable)<0)return 0;
    int mode,width,height;if(sceDisplayGetMode(&mode,&width,&height)<0)return 0;
    if(!cable){stable_since=0;return 0;}
    if(mode==0 && sceKernelInitKeyConfig()==PSP_INIT_KEYCONFIG_VSH) {
        void *frame=NULL;int stride=0,format=-1;
        /* Cold boot and returning from a game have different timing. Wait for
         * the shell and an established LCD layout, not just eight seconds
         * since this plugin thread happened to start. Buffer addresses may
         * alternate normally, so only layout changes reset the timer. */
        int ready=sceKernelGetSystemStatus()==0x20000 &&
            sceKernelFindModuleByName("vsh_module") && sceKernelFindModuleByName("scePaf_Module") &&
            width==480 && height==272 &&
            sceDisplayGetFrameBuf(&frame,&stride,&format,1)>=0 && frame &&
            (stride==512||stride==1024) && format>=0 && format<=3;
        if(!ready){stable_since=0;if(!ready_logged){controller_log("TV waiting for ready XMB/framebuffer",0);ready_logged=1;}return 0;}
        if(!stable_since||stride!=last_stride||format!=last_format){
            stable_since=now;last_stride=stride;last_format=format;return 0;
        }
        if(now-stable_since<2000000)return 0;
        controller_log("TV XMB layout stable",format);
    }
    attempted=1;
    controller_log("TV detected cable",cable);controller_log("TV existing display mode",mode);
    if(mode!=0)return 0; /* Never deliberately toggle an already external mode. */
    controller_log("TV request via Sony display button",controller_tvout);
    /* Log writes above can block for seconds on a Memory Stick. Arm only
     * after the final write, immediately before the caller injects SCREEN;
     * never spend the hold interval before the key has even been pressed. */
    controller_tv_hold_until=sceKernelGetSystemTimeWide()+6000000;external_since=0;
    return PSP_CTRL_SCREEN;
}

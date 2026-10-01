/* SPDX-License-Identifier: GPL-2.0-or-later
 * Ask Sony's shell/impose handler to perform its normal display-button
 * transition. Do not impose PSPStreamer's 720px framebuffer on a game.
 * 0 off, 1 connected controller, 2 application start (also VSH/POPS). */
static int controller_tvout;
static int (*controller_check_cable)(int *);
static unsigned controller_tv_button(unsigned long long now,int connected,int streamer,int permitted) {
    static unsigned long long ready_at,hold_until,next_check;
    static int attempted;
    if(!ready_at)ready_at=now+8000000;
    if(!controller_tvout || !permitted || streamer || controller_suspended || controller_disabled){hold_until=0;return 0;}
    if(hold_until){
        int mode,width,height;
        if(now>=hold_until || sceDisplayGetMode(&mode,&width,&height)<0 || mode!=0){
            hold_until=0;controller_log("TV display-button request released",0);return 0;
        }
        return PSP_CTRL_SCREEN;
    }
    if(attempted || now<ready_at || now<next_check || (controller_tvout==1 && !connected))return 0;
    next_check=now+1000000;
    if(sceKernelGetModel()==0){attempted=1;controller_log("TV unavailable on PSP-1000",0);return 0;}
    if(!controller_check_cable)controller_check_cable=(void *)sctrlHENFindFunction("sceImpose_Driver","sceImpose_driver",0x116CFF64);
    if(!controller_check_cable)return 0;
    int cable=0;if(controller_check_cable(&cable)<0)return 0;
    int mode,width,height;if(sceDisplayGetMode(&mode,&width,&height)<0)return 0;
    attempted=1;
    controller_log("TV detected cable",cable);controller_log("TV existing display mode",mode);
    if(mode!=0)return 0; /* Never deliberately toggle an already external mode. */
    hold_until=now+6000000;
    controller_log("TV request via Sony display button",controller_tvout);
    return PSP_CTRL_SCREEN;
}

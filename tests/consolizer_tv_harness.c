/* Targeted test of the real bounded TV activation policy, not PSP hardware. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#define PSP_CTRL_SCREEN 0x400000U
#define PSP_INIT_KEYCONFIG_VSH 0x100
static int controller_suspended,controller_disabled;
static int cable_present=1,display_mode,model=2,logs,context=0x200,boot_status=0x20000,shell_ready=1;
static int sceKernelInitKeyConfig(void){return context;}
static int sceKernelGetSystemStatus(void){return boot_status;}
static void *sceKernelFindModuleByName(const char *name){(void)name;return shell_ready?(void *)1:NULL;}
static int sceDisplayGetFrameBuf(void **frame,int *stride,int *format,int sync){
    (void)sync;*frame=(void *)0x04000000;*stride=512;*format=3;return 0;
}
static int check_cable(int *cable){*cable=2;return cable_present?0:-1;}
static uintptr_t sctrlHENFindFunction(const char *a,const char *b,unsigned nid){(void)a;(void)b;(void)nid;return (uintptr_t)check_cable;}
static int sceKernelGetModel(void){return model;}
static int sceDisplayGetMode(int *mode,int *width,int *height){*mode=display_mode;*width=480;*height=272;return 0;}
static void controller_log(const char *message,int result){(void)message;(void)result;logs++;}
#include "psp-controller/tvout.h"
int main(int argc,char **argv){
    int scenario=argc>1?atoi(argv[1]):0;
    controller_tvout=scenario==1?2:1;
    if(scenario==2)display_mode=0x1d2;
    if(scenario==3)cable_present=0;
    if(scenario==4)model=0;
    if(scenario==5)controller_tvout=0;
    if(scenario==6){
        context=PSP_INIT_KEYCONFIG_VSH;boot_status=0;
        assert(!controller_tv_button(1,1,0,1));
        assert(!controller_tv_button(9000000,1,0,1));
        boot_status=0x20000;shell_ready=0;
        assert(!controller_tv_button(10000000,1,0,1));
        shell_ready=1;
        assert(!controller_tv_button(11000000,1,0,1));
        assert(!controller_tv_button(12000000,1,0,1));
        assert(controller_tv_button(13000000,1,0,1)==PSP_CTRL_SCREEN);
        display_mode=0x1d2;
        assert(!controller_tv_button(14000000,1,0,1));
        return 0;
    }
    assert(!controller_tv_button(1,1,0,1));
    if(scenario>=2){assert(!controller_tv_button(9000000,1,0,1));return 0;}
    if(!scenario)assert(!controller_tv_button(8500000,0,0,1));
    assert(!controller_tv_button(8800000,1,1,1)); /* app owns output */
    assert(controller_tv_button(9000000,!scenario,0,1)==PSP_CTRL_SCREEN);
    assert(controller_tv_button(10000000,1,0,1)==PSP_CTRL_SCREEN);
    if(scenario==1)controller_suspended=1;
    else display_mode=0x1d2;
    assert(!controller_tv_button(11000000,1,0,1));
    controller_suspended=0;display_mode=0;
    assert(!controller_tv_button(25000000,1,0,1)); /* never toggle repeatedly */
    assert(logs>=3);return 0;
}

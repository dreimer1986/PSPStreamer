#include <assert.h>
#include <stddef.h>
#include <string.h>
#define PSP_CTRL_SCREEN 0x400000
#define PSP_INIT_KEYCONFIG_VSH 0x100
static int controller_suspended,controller_disabled;
static int query_rc,display_mode,logs;
static void controller_log(const char *s,int rc){(void)s;(void)rc;logs++;}
static int sceDisplayGetMode(int *mode,int *w,int *h){*mode=display_mode;*w=480;*h=272;return query_rc;}
static int sceKernelGetModel(void){return 2;}
static int cable_check(int *c){*c=2;return 0;}
static void *sctrlHENFindFunction(const char *a,const char *b,unsigned c){(void)a;(void)b;(void)c;return (void *)cable_check;}
static int sceKernelInitKeyConfig(void){return PSP_INIT_KEYCONFIG_VSH;}
static int sceKernelGetSystemStatus(void){return 0x20000;}
static void *sceKernelFindModuleByName(const char *s){(void)s;return (void *)1;}
static int sceDisplayGetFrameBuf(void **p,int *s,int *f,int n){(void)n;*p=(void *)1;*s=512;*f=3;return 0;}
#include "../psp-controller/tvout.h"
int main(int argc,char **argv){
    assert(argc==2);controller_tvout=2;
    assert(!controller_tv_button(1,0,0,1));
    assert(!controller_tv_button(8000001,0,0,1));
    assert(!controller_tv_button(9000001,0,0,1));
    assert(controller_tv_button(10000001,0,0,1)==PSP_CTRL_SCREEN);
    assert(controller_tv_hold_until==16000001);
    if(!strcmp(argv[1],"confirm")){
        query_rc=-1;assert(controller_tv_button(10100001,0,0,1)==PSP_CTRL_SCREEN);
        query_rc=0;display_mode=1;assert(controller_tv_button(10200001,0,0,1)==PSP_CTRL_SCREEN);
        display_mode=0;assert(controller_tv_button(10300001,0,0,1)==PSP_CTRL_SCREEN);
        display_mode=1;assert(controller_tv_button(10400001,0,0,1)==PSP_CTRL_SCREEN);
        assert(controller_tv_button(10699999,0,0,1)==PSP_CTRL_SCREEN);
        assert(!controller_tv_button(10700001,0,0,1));
    }else if(!strcmp(argv[1],"timeout")){
        assert(controller_tv_button(15999999,0,0,1)==PSP_CTRL_SCREEN);
        assert(!controller_tv_button(16000001,0,0,1));
    }else{
        controller_suspended=1;assert(!controller_tv_button(10100001,0,0,1));
    }
    assert(!controller_tv_hold_until);
    /* No blind repeated toggle after success, timeout, or suspension. */
    controller_suspended=0;assert(!controller_tv_button(20000001,0,0,1));
    return 0;
}

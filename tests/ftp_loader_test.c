#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#define PSP_FTP_LOADER_HOST_TEST
#define PSP_MODULE_INFO(...)
#define PSP_INIT_KEYCONFIG_VSH 1
#define PSP_O_WRONLY 1
#define PSP_O_CREAT 2
#define PSP_O_APPEND 4
typedef int SceUID;
typedef unsigned SceSize;
typedef unsigned SceUInt;
static unsigned long long tick;
static int io,loads,starts,unloads,create_rc=1,start_rc,load_rc=2,stop_rc,wait_rc;
static int ready=1,key=1,cancel;
static int sceKernelInitKeyConfig(void){return key;}
static unsigned long long sceKernelGetSystemTimeWide(void){return tick;}
static int sceKernelGetSystemStatus(void){return ready?0x20000:0;}
static void *sceKernelFindModuleByName(const char *s){return s[0]=='P'?NULL:(ready?(void*)1:NULL);}
static int sceIoOpen(const char *s,int f,int m){(void)s;(void)f;(void)m;io++;return -1;}
static int sceIoWrite(int f,const void *s,int n){(void)f;(void)s;return n;}
static int sceIoClose(int f){(void)f;return 0;}
static int sceKernelPartitionTotalFreeMemSize(int p){(void)p;return 65536;}
static int sceKernelPartitionMaxFreeMemSize(int p){(void)p;return 32768;}
static int sceKernelLoadModule(const char *s,int f,void *p){(void)s;(void)f;(void)p;assert(tick>=25000000);loads++;return load_rc;}
static int sceKernelStartModule(int id,int n,void *a,int *s,void *o){(void)id;(void)n;(void)a;(void)o;*s=0;starts++;return 0;}
static int sceKernelStopModule(int id,int n,void *a,int *s,void *o){(void)id;(void)n;(void)a;(void)o;*s=0;return stop_rc;}
static int sceKernelUnloadModule(int id){(void)id;unloads++;return 0;}
static int sceKernelCreateThread(const char *n,int(*f)(SceSize,void*),int p,int s,int a,void *o){(void)n;(void)f;(void)p;(void)a;(void)o;assert(s==4096);return create_rc;}
static int sceKernelStartThread(int id,int n,void *a){(void)id;(void)n;(void)a;return start_rc;}
static int sceKernelDeleteThread(int id){(void)id;return 0;}
static int sceKernelWaitThreadEnd(int id,SceUInt *t){(void)id;(void)t;return wait_rc;}
static int sceKernelDelayThread(unsigned us);
#include "../psp-ftp/loader.c"
static int sceKernelDelayThread(unsigned us){tick+=us;if(cancel && tick>=1000000)running=0;return 0;}
static void reset(void){tick=0;io=loads=starts=unloads=0;create_rc=1;start_rc=stop_rc=wait_rc=0;load_rc=2;ready=key=1;cancel=0;running=0;thread=service=-1;}
int main(void){
    reset();assert(module_start(0,NULL)==0);assert(!io&&!loads);worker(0,NULL);
    assert(loads==1&&starts==1);assert(module_stop(0,NULL)==0&&unloads==1);
    reset();load_rc=-1;module_start(0,NULL);worker(0,NULL);assert(loads==1&&!starts&&service==-1);assert(!module_stop(0,NULL));
    reset();ready=0;cancel=1;module_start(0,NULL);worker(0,NULL);assert(!loads&&!io);assert(!module_stop(0,NULL));
    reset();create_rc=-1;assert(!module_start(0,NULL)&&!running&&!io);
    reset();start_rc=-1;assert(!module_start(0,NULL)&&!running&&thread==-1&&!io);
    reset();key=0;assert(!module_start(0,NULL)&&thread==-1);
    reset();module_start(0,NULL);worker(0,NULL);stop_rc=-1;assert(module_stop(0,NULL)<0&&!unloads&&service==2);
    reset();module_start(0,NULL);worker(0,NULL);wait_rc=-1;assert(module_stop(0,NULL)<0&&!unloads);
    puts("FTP loader lifecycle checks passed");return 0;
}

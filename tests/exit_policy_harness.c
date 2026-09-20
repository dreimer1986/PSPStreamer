#include <assert.h>
#include <stddef.h>
#include "../psp-overclock/control_api.h"
static int available=1,remaining,polls,delays,final_result,stop_remaining;
static void sceKernelDelayThread(int us){assert(us==10000);delays++;}
static int sceIoDevctl(const char *device,unsigned cmd,void *in,int il,void *out,int ol){
    assert(device&&!in&&!il&&!out&&!ol);
    if(!available)return -2;
    if(cmd==OC_CMD_PREPARE_EXIT)return 0;
    assert(cmd==OC_CMD_EXIT_STATUS);polls++;
    return remaining-->0?1:final_result;
}
static int stop(void){return stop_remaining-->0?0:1;}
#include "exit_policy.h"
int main(void){
    available=0;assert(prepare_oc_exit()==-2&&!delays&&!polls);
    available=1;remaining=3;assert(prepare_oc_exit()==0&&delays==3&&polls==4);
    delays=polls=0;remaining=1000;
    assert(prepare_oc_exit()<0&&delays==250&&polls==250);
    remaining=0;delays=polls=0;final_result=-3;
    assert(prepare_oc_exit()==-3&&!delays&&polls==1);
    delays=0;stop_remaining=3;assert(exit_join_worker(stop)==1&&delays==3);
    delays=0;stop_remaining=1000;assert(exit_join_worker(stop)==0&&delays==200);
    return 0;
}

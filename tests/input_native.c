#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int SceSize;
typedef struct {unsigned int Buttons;unsigned char Lx,Ly;} SceCtrlData;
#define PSP_THREAD_ATTR_USER 0
static int network_ready,http_ready,have_cached_server_address;
static unsigned long long tick;
static unsigned long long sceKernelGetSystemTimeWide(void){return tick;}
static int sceKernelWaitThreadEnd(int id,void *wait){(void)id;(void)wait;return 0;}
static void sceKernelDeleteThread(int id){(void)id;}
static int sceKernelCreateThread(const char *name,int (*fn)(SceSize,void*),int priority,int stack,int attr,void *arg){
    (void)name;(void)fn;(void)priority;(void)stack;(void)attr;(void)arg;return -1;
}
static int sceKernelStartThread(int id,int size,void *arg){(void)id;(void)size;(void)arg;return -1;}
static int remote_http_get_budget(const char *path,char *buffer,int capacity,volatile int *run,int ms){
    (void)path;(void)buffer;(void)capacity;(void)run;(void)ms;return -1;
}
#include "remote_input_impl.h"
int main(void){
    SceCtrlData p={8,128,128};tick=100;
    strcpy(input_reply,"1 1 768 200 128 900 0 -");input_receive(tick);input_remote_tick(&p);
    assert(input_next==tick+2000000ULL); /* No rapid polling for active keys. */
    assert(p.Buttons==(8|768)&&p.Lx==200);
    p=(SceCtrlData){0,90,128};input_remote_tick(&p);assert(p.Lx==90);
    tick+=70000;p=(SceCtrlData){0,128,128};input_remote_tick(&p);
    assert(!p.Buttons&&p.Lx==200); /* Delayed release must not cause menu repeat. */
    tick+=901000;p=(SceCtrlData){0,128,128};input_remote_tick(&p);assert(!p.Buttons&&p.Lx==128);
    strcpy(input_reply,"1 3 16 128 128 900 0 -");input_receive(tick);
    assert(input_next==tick+2000000ULL);
    tick+=200000;p=(SceCtrlData){0,128,128};input_remote_tick(&p);assert(p.Buttons==16);
    tick+=701000;p=(SceCtrlData){0,128,128};input_remote_tick(&p);assert(!p.Buttons);
    input_text_begin(32,1);char text[32]="old";
    snprintf(input_reply,sizeof(input_reply),"2 2 0 128 128 900 %d 4772c3bcc39f65",input_dialog);
    input_receive(tick);assert(input_text_take(text,32)&&!strcmp(text,"Grüße"));assert(!input_text_take(text,32));
    input_text_end();input_text_begin(32,0);
    strcpy(input_reply,"3 2 0 128 128 900 1 626164");input_receive(tick);assert(!input_text_take(text,32));
    snprintf(input_reply,sizeof(input_reply),"4 2 0 128 128 900 %d -",input_dialog);
    input_receive(tick);assert(input_text_take(text,32)&&!text[0]);
    strcpy(input_reply,"5 1 65536 128 128 100 0 -");input_receive(tick);assert(!input_buttons);
    input_remote_stop();assert(!input_client[0]&&!input_ack&&!input_buttons);
    return 0;
}

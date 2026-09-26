#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
typedef unsigned int SceSize;
typedef struct {unsigned int Buttons;} SceCtrlData;
#define PSP_CTRL_CIRCLE 1
#define PSP_CTRL_START 2
static int music_transition;
#define PSP_THREAD_ATTR_USER 0
static pthread_t worker;
static int (*entry)(SceSize,void *);
static atomic_int finished;
static int cancel_input,draws,deleted,fail_create,fail_start;
static void nap(void) {struct timespec t={0,1000000};nanosleep(&t,NULL);}
static unsigned long long sceKernelGetSystemTimeWide(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return t.tv_sec*1000000ULL+t.tv_nsec/1000;
}
static void *run(void *p) {(void)p;entry(0,NULL);finished=1;return NULL;}
static int sceKernelCreateThread(const char *n,int (*fn)(SceSize,void *),int priority,int stack,int attr,void *p) {
    (void)n;(void)priority;(void)attr;(void)p;assert(stack>=0x10000);
    entry=fn;finished=0;return fail_create?-17:1;
}
static int sceKernelStartThread(int id,int args,void *p) {
    (void)id;(void)args;(void)p;if(fail_start)return -18;
    assert(!pthread_create(&worker,NULL,run,NULL));return 0;
}
static int sceKernelWaitThreadEnd(int id,unsigned int *timeout) {
    (void)id;assert(timeout && *timeout==1);
    if(!finished)return -1;
    assert(!pthread_join(worker,NULL));return 0;
}
static void sceKernelDeleteThread(int id) {(void)id;deleted++;}
static void keep_awake(void) {}
static void sceCtrlReadBufferPositive(SceCtrlData *pad,int n) {(void)n;pad->Buttons=cancel_input?1:0;}
static void sceKernelDelayThread(int us) {(void)us;nap();}
struct in_addr {int unused;};
typedef struct {int status,retryable;const char *stage;} RemoteHttpReport;
static int resolve_server_address(struct in_addr *a){(void)a;return 0;}
static void recovery_log(const char *e,int r,int s,const char *p){(void)e;(void)r;(void)s;(void)p;}
static int remote_http_request_policy(const char *path,char *buffer,int capacity,volatile int *running,int budget,const char *body,RemoteHttpReport *report) {
    (void)body;report->status=200;report->retryable=1;report->stage="receive";
    assert(!strcmp(path,"/api/subtitles/test"));assert(capacity==64 && budget==210000);
    for(int i=0;i<200 && *running;i++)nap();
    if(!*running)return -1005;
    strcpy(buffer,"{\"t\":\"text\",\"c\":[]}");return strlen(buffer);
}
#include "media_request.h"
static void media_wait_draw(int subtitles,unsigned int seconds,int cancelling) {
    assert(subtitles==1);(void)seconds;(void)cancelling;draws++;
}
int main(void) {
    char buffer[64];
    assert(media_request_get("/api/subtitles/test",buffer,sizeof(buffer),210000,1)>0);
    assert(draws>=2 && deleted==1 && strstr(buffer,"text"));
    cancel_input=1;
    assert(media_request_get("/api/subtitles/test",buffer,sizeof(buffer),210000,1)==MEDIA_REQUEST_CANCELLED);
    assert(deleted==2 && finished && !media_request_running);
    cancel_input=0;
    assert(media_request_get("/api/subtitles/test",buffer,sizeof(buffer),210000,1)>0);
    assert(deleted==3);
    fail_create=1;assert(media_request_get("",buffer,64,210000,1)==-17);
    fail_create=0;fail_start=1;assert(media_request_get("",buffer,64,210000,1)==-18);
    assert(deleted==4);
    return 0;
}

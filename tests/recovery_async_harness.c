#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
typedef int SceUID;
typedef unsigned SceSize;
#define PSP_O_WRONLY 1
#define PSP_O_CREAT 2
#define PSP_O_APPEND 4
static int debug_enabled=1,start_failure,deleted;
static pthread_t worker;
static int (*entry)(SceSize,void *);
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static int entered,release_write;
static char disk[32768];
static unsigned used,opens,closes;
static unsigned long long sceKernelGetSystemTimeWide(void){
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (unsigned long long)t.tv_sec*1000000+t.tv_nsec/1000;
}
static void sceKernelDelayThread(unsigned us){
    struct timespec t={us/1000000,(us%1000000)*1000};nanosleep(&t,NULL);
}
static void *run(void *p){(void)p;entry(0,NULL);return NULL;}
static int sceKernelCreateThread(const char *n,int (*fn)(SceSize,void *),int p,int s,int a,void *o){
    (void)n;(void)p;(void)s;(void)a;(void)o;entry=fn;return 7;
}
static int sceKernelStartThread(int t,int a,void *p){
    assert(t==7);(void)a;(void)p;
    return start_failure?-1:pthread_create(&worker,NULL,run,NULL);
}
static int sceKernelWaitThreadEnd(int t,void *p){assert(t==7);(void)p;return pthread_join(worker,NULL);}
static int sceKernelDeleteThread(int t){assert(t==7);deleted++;return 0;}
static int sceIoOpen(const char *p,int f,int m){(void)p;(void)f;(void)m;opens++;return 4;}
static int sceIoWrite(int fd,const void *data,unsigned n){
    assert(fd==4);
    pthread_mutex_lock(&gate);entered=1;pthread_cond_broadcast(&changed);
    while(!release_write)pthread_cond_wait(&changed,&gate);
    pthread_mutex_unlock(&gate);
    if(n>53)n=53; /* Exercise partial writes. */
    assert(used+n<sizeof(disk));memcpy(disk+used,data,n);used+=n;return n;
}
static int sceIoClose(int fd){assert(fd==4);closes++;return 0;}
#include "../psp-client/recovery_log.h"
int main(void){
    recovery_batch_begin();assert(recovery_writer==7);
    recovery_log("first",0,0,"held by writer");
    recovery_batch_next=0;recovery_flush_due();
    pthread_mutex_lock(&gate);
    while(!entered)pthread_cond_wait(&changed,&gate);
    pthread_mutex_unlock(&gate);
    /* Writer is blocked inside card I/O. Producers and due-flush still return,
     * and use a separate bounded RAM buffer, not the writer's live buffer. */
    recovery_log("second",0,0,"queued during blocked write");
    recovery_download_timing();
    recovery_batch_next=0;recovery_flush_due();
    for(int i=0;i<100;i++)recovery_log("fill",0,0,"bounded");
    assert(recovery_batch_size<=sizeof(recovery_batch));
    pthread_mutex_lock(&gate);release_write=1;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&gate);
    recovery_batch_end();
    assert(recovery_writer==-1&&!recovery_batch_active&&deleted==1);
    assert(opens==closes && recovery_flush_count>=2);
    assert(strstr(disk,"first")&&strstr(disk,"second")&&strstr(disk,"diagnostic overflow"));
    assert(strstr(disk,"first")<strstr(disk,"second"));
    /* Repeated download, thread-start failure, and disabled diagnostics. */
    start_failure=1;recovery_batch_begin();assert(recovery_writer==-1&&deleted==2);
    recovery_log("fallback",0,0,"synchronous");recovery_batch_next=0;recovery_flush_due();
    recovery_batch_end();assert(strstr(disk,"fallback")&&opens==closes);
    debug_enabled=0;unsigned previous=opens;
    recovery_batch_begin();recovery_log("disabled",0,0,"");recovery_batch_end();
    assert(opens==previous&&deleted==2);
    puts("Async logger: blocked I/O, bounded buffers, drain, restart and fallback OK");
}

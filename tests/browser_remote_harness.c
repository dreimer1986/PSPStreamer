#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef unsigned int SceSize;
#define PSP_THREAD_ATTR_USER 0
static pthread_t worker;
static int (*entry)(SceSize, void *);
static atomic_int requests, release_reply, entered;
static int fail_create, fail_start, deleted, joined, fail_http;
static unsigned long long fake_now;
static void pause_ms(void) { struct timespec t={0,1000000}; nanosleep(&t,NULL); }
static unsigned long long sceKernelGetSystemTimeWide(void) { return fake_now; }
static void *run(void *unused) { (void)unused; entry(0,NULL); return NULL; }
static int sceKernelCreateThread(const char *name,int (*fn)(SceSize,void *),int priority,
                                  int stack,int attributes,void *options) {
    (void)name;(void)priority;(void)stack;(void)attributes;(void)options;
    entry=fn;return fail_create?-1:1;
}
static int sceKernelStartThread(int id,int args,void *argp) {
    (void)id;(void)args;(void)argp;
    if(fail_start)return -1;
    assert(!pthread_create(&worker,NULL,run,NULL));return 0;
}
static void sceKernelWaitThreadEnd(int id,void *timeout) {
    (void)id;(void)timeout;assert(!pthread_join(worker,NULL));joined++;
}
static void sceKernelDeleteThread(int id) { (void)id;deleted++; }
static int remote_http_get(const char *path,char *reply,int capacity,volatile int *running) {
    assert(strstr(path,"/api/remote/next?after=7"));assert(capacity>=64);
    requests++;entered=1;
    while(*running && !release_reply)pause_ms();
    if(!*running || fail_http)return -1;
    strcpy(reply,"{\"seq\":8,\"action\":\"play\",\"id\":\"example\"}");return strlen(reply);
}
#include "browser_remote.h"

int main(void) {
    /* A stalled HTTPS request must not block repeated UI polling. */
    assert(!browser_remote_poll(7));
    while(!entered)pause_ms();
    for(int i=0;i<10000;i++)assert(!browser_remote_poll(7));
    assert(requests==1);
    browser_remote_stop();
    assert(joined==1 && deleted==1 && browser_remote_thread_id==-1);
    assert(!browser_remote_poll(7));assert(requests==1); /* Rate limit. */
    fake_now=1000000;entered=0;release_reply=1;
    assert(!browser_remote_poll(7));
    const char *reply=NULL;
    for(int i=0;i<1000 && !reply;i++){pause_ms();reply=browser_remote_poll(7);}
    assert(reply && strstr(reply,"example") && joined==2 && deleted==2);
    assert(browser_remote_thread_id==-1);
    assert(!browser_remote_poll(7));
    fake_now+=1000000;fail_create=1;assert(!browser_remote_poll(7));
    assert(!browser_remote_running && browser_remote_thread_id==-1);
    fake_now+=1000000;fail_create=0;fail_start=1;assert(!browser_remote_poll(7));
    assert(!browser_remote_running && browser_remote_thread_id==-1 && deleted==3);
    fake_now+=1000000;fail_start=0;fail_http=1;
    assert(!browser_remote_poll(7));
    for(int i=0;i<1000 && browser_remote_thread_id>=0;i++){pause_ms();assert(!browser_remote_poll(7));}
    assert(browser_remote_thread_id==-1 && joined==3 && deleted==4);
    browser_remote_stop();assert(deleted==4);
    return 0;
}

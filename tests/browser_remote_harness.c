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
static atomic_int requests, release_reply, entered, finished, ignore_cancel;
static int fail_create, fail_start, deleted, joined, fail_http;
static unsigned long long fake_now;
static void pause_ms(void) { struct timespec t={0,1000000}; nanosleep(&t,NULL); }
static void sceKernelDelayThread(int us) {(void)us;pause_ms();}
static unsigned long long sceKernelGetSystemTimeWide(void) { return fake_now; }
static void *run(void *unused) { (void)unused; entry(0,NULL); finished=1; return NULL; }
static int sceKernelCreateThread(const char *name,int (*fn)(SceSize,void *),int priority,
                                  int stack,int attributes,void *options) {
    (void)name;(void)priority;(void)stack;(void)attributes;(void)options;
    entry=fn;return fail_create?-1:1;
}
static int sceKernelStartThread(int id,int args,void *argp) {
    (void)id;(void)args;(void)argp;
    if(fail_start)return -1;
    finished=0;assert(!pthread_create(&worker,NULL,run,NULL));return 0;
}
static int sceKernelWaitThreadEnd(int id,void *timeout) {
    (void)id;assert(timeout && *(unsigned int *)timeout==1);
    if(!finished)return -1;
    assert(!pthread_join(worker,NULL));joined++;return 0;
}
static void sceKernelDeleteThread(int id) { (void)id;deleted++; }
static int remote_http_get(const char *path,char *reply,int capacity,volatile int *running) {
    assert(strstr(path,"/api/remote/next?after=7"));assert(capacity>=64);
    requests++;entered=1;
    while((*running || ignore_cancel) && !release_reply)pause_ms();
    if(!*running || fail_http)return -1;
    strcpy(reply,"{\"seq\":8,\"action\":\"play\",\"id\":\"example\"}");return strlen(reply);
}
static int art_requested,art_delivered,art_completed;
static char menu_art_job[32],menu_art_wanted[32];
static int menu_art_schedule(void){int result=art_requested;art_requested=0;return result;}
static void menu_art_download(volatile int *running){
    entered=1;
    while(*running && !release_reply)pause_ms();
}
static void menu_art_complete(int deliver){art_delivered+=deliver;art_completed++;}
#include "browser_remote.h"
#define ID_SIZE 512
static atomic_int library_entered, library_release;
static int library_failures,library_calls,library_error=-1005;
static int library_fetch(const char *path,volatile int *running) {
    assert(!strcmp(path,"/api/library?path=:plex:s4"));library_entered=1;library_calls++;
    while(*running && !library_release)pause_ms();
    if(library_failures){library_failures--;return library_error;}
    return *running?123:-1005;
}
#include "library_request.h"

int main(void) {
    /* A stalled HTTPS request must not block repeated UI polling. */
    assert(!browser_remote_poll(7));
    while(!entered)pause_ms();
    for(int i=0;i<10000;i++)assert(!browser_remote_poll(7));
    assert(requests==1);
    while(!browser_remote_stop())pause_ms();
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
    assert(browser_remote_stop());assert(deleted==4);
    /* A non-cooperative transport must not trap Stop in an infinite join. */
    fake_now+=1000000;fail_http=0;entered=0;release_reply=0;ignore_cancel=1;
    assert(!browser_remote_poll(7));while(!entered)pause_ms();
    for(int i=0;i<10000;i++)assert(!browser_remote_stop());
    assert(browser_remote_thread_id>=0);
    /* No directory request may overlap a cancelled, still-owned TLS socket. */
    strcpy(library_loaded_path,":plex:");
    strcpy(library_request_path,"/api/library?path=:plex:s4");
    library_started=fake_now;assert(library_started>0);
    library_pending=1;library_cancelled=0;
    for(int i=0;i<10000;i++)assert(library_request_poll()==0 && !library_entered);
    ignore_cancel=0;release_reply=1;
    for(int i=0;i<1000 && !library_entered;i++){library_request_poll();pause_ms();}
    assert(library_entered && library_pending);
    for(int i=0;i<10000;i++)assert(library_request_poll()==0);
    library_cancel();
    int completed=0;
    for(int i=0;i<1000 && !completed;i++){completed=library_request_poll();pause_ms();}
    assert(completed==1 && library_cancelled && library_result==-1005 && library_thread==-1);
    assert(!strcmp(library_loaded_path,":plex:"));
    library_pending=1;library_cancelled=0;library_release=1;completed=0;
    for(int i=0;i<1000 && !completed;i++){completed=library_request_poll();pause_ms();}
    assert(completed==1 && library_result==123 && library_thread==-1);
    assert(library_request_poll()==-1);
    for(int failures=1;failures<=3;failures++) {
        library_failures=failures;library_calls=0;library_pending=1;library_cancelled=0;completed=0;
        for(int i=0;i<1000 && !completed;i++){completed=library_request_poll();pause_ms();}
        assert(completed==1&&library_calls==(failures<3?failures+1:3));
        assert(library_result==(failures<3?123:-1005));
    }
    library_failures=3;library_error=-1003;library_calls=0;library_pending=1;completed=0;
    for(int i=0;i<1000 && !completed;i++){completed=library_request_poll();pause_ms();}
    assert(completed==1&&library_calls==1&&library_result==-1003);
    /* Art uses the same worker but must never replay its previous Play reply. */
    browser_remote_next=fake_now+1000000;art_requested=1;entered=0;release_reply=0;
    assert(!browser_remote_poll(7));while(!entered)pause_ms();
    release_reply=1;
    for(int i=0;i<1000 && browser_remote_thread_id>=0;i++){pause_ms();assert(!browser_remote_poll(7));}
    assert(browser_remote_thread_id==-1 && art_completed==1 && art_delivered==1);
    assert(browser_remote_next==0); /* Remote control resumes immediately. */
    browser_remote_next=fake_now+1000000;art_requested=1;entered=0;release_reply=0;
    assert(!browser_remote_poll(7));while(!entered)pause_ms();
    while(!browser_remote_stop())pause_ms();
    assert(art_completed==2 && art_delivered==1);
    /* TV drawing can take longer than the remote interval: art must still
     * get a turn after a control response, not only inside the idle gap. */
    browser_remote_next=0;entered=0;release_reply=1;
    assert(!browser_remote_poll(7));reply=NULL;
    for(int i=0;i<1000 && !reply;i++){pause_ms();reply=browser_remote_poll(7);}
    assert(reply && browser_art_turn);
    fake_now+=2000000;art_requested=1;entered=0;release_reply=0;
    assert(!browser_remote_poll(7));while(!entered)pause_ms();
    assert(browser_art_task);
    assert(browser_art_busy());
    strcpy(menu_art_wanted,"new selection");
    assert(!browser_remote_poll(7));
    assert(!browser_remote_running); /* Cancel old art without waiting 10 s. */
    while(!browser_remote_stop())pause_ms();
    return 0;
}

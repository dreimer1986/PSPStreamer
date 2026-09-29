#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
typedef unsigned SceSize;
typedef struct {int start,end,x,y,width,height,canvas_width,canvas_height;} BitmapCue;
#define ID_SIZE 512
#define PSP_THREAD_ATTR_USER 0
static BitmapCue cues[3]={{1000,2000,0,0,4,4,480,272},{2200,3000,0,0,8,8,480,272},{3200,4000,0,0,4,4,480,272}};
static BitmapCue *bitmap_cues=cues;
static int bitmap_client_side=1,bitmap_cue_count=3,bitmap_loaded_cue=-1;
static int offline_active,selected_subtitle_track,fetches,fail_index=-1,notes,released;
static char audio_media_id[]="test";
static volatile int block_fetch,entered;
static pthread_t worker,owner;
static int (*entry)(SceSize,void *);
static void pause_ms(void){struct timespec t={0,1000000};nanosleep(&t,NULL);}
static void sceKernelDelayThread(unsigned us){(void)us;pause_ms();}
static int sceKernelCreateThread(const char *name,int (*fn)(SceSize,void *),int priority,int stack,int attr,void *opts){
    (void)name;(void)priority;(void)stack;(void)attr;(void)opts;entry=fn;return 1;
}
static void *run(void *unused){(void)unused;entry(0,NULL);return NULL;}
static int sceKernelStartThread(int id,int args,void *p){(void)id;(void)args;(void)p;return pthread_create(&worker,NULL,run,NULL);}
static int sceKernelWaitThreadEnd(int id,void *p){(void)id;(void)p;return pthread_join(worker,NULL);}
static void sceKernelDeleteThread(int id){(void)id;}
static void network_worker_finished(const char *name){(void)name;released++;}
static void recovery_log(const char *event,int rc,int status,const char *detail){(void)event;(void)rc;(void)status;(void)detail;notes++;}
static int offline_bitmap(int index,unsigned char *out,int capacity){
    int size=1024+cues[index].width*cues[index].height;assert(size<=capacity);
    memset(out,index+1,size);return size;
}
static int remote_http_get_budget(const char *path,char *out,int capacity,volatile int *running,int budget){
    assert(!pthread_equal(pthread_self(),owner));assert(budget==3000);assert(strstr(path,"lcd=1"));
    int index=atoi(strstr(path,"cue=")+4);fetches++;entered=1;
    while(block_fetch&&*running)pause_ms();
    if(!*running||index==fail_index)return -1005;
    return offline_bitmap(index,(unsigned char *)out,capacity);
}
#include "bitmap_overlay.h"
static void ready(void){for(int i=0;i<2000&&!bitmap_ready;i++)pause_ms();assert(bitmap_ready);}
int main(void){
    owner=pthread_self();bitmap_start();assert(bitmap_running);
    int index=-1;assert(!bitmap_pixels(0,&index));ready();
    const unsigned char *p=bitmap_pixels(1000,&index);assert(p&&index==0&&p[0]==1);
    ready();assert(p[0]==1); /* Back-bank prefetch cannot touch front. */
    p=bitmap_pixels(2200,&index);assert(p&&index==1&&p[0]==2);
    ready();p=bitmap_pixels(3200,&index);assert(p&&index==2&&p[0]==3);
    assert(fetches==3);bitmap_stop();assert(released==1&&!bitmap_banks[0]&&!bitmap_banks[1]);
    fail_index=0;fetches=0;bitmap_start();bitmap_pixels(1000,&index);
    for(int i=0;i<1000&&!bitmap_attempted[0];i++)pause_ms();
    for(int i=0;i<1000;i++){bitmap_pixels(1000,&index);pause_ms();}
    assert(fetches<=2&&notes>=1); /* Failed cue is not fetched every frame. */
    bitmap_stop();
    fail_index=-1;block_fetch=1;entered=0;bitmap_start();bitmap_pixels(1000,&index);
    for(int i=0;i<1000&&!entered;i++)pause_ms();assert(entered);
    for(int i=0;i<10000;i++)assert(!bitmap_pixels(1000,&index));
    bitmap_stop();assert(!bitmap_banks[0]&&!bitmap_banks[1]); /* Cancel joins owner before free. */
    BitmapCue bad={0,1,0,0,1920,1079,1920,1080};assert(!bitmap_payload_size(&bad));
    bad=cues[0];bad.canvas_width=0;assert(!bitmap_payload_size(&bad));
    bad=cues[0];bad.x=-1;assert(!bitmap_payload_size(&bad));
    bad=cues[0];bad.width=2147483647;assert(!bitmap_payload_size(&bad));
    offline_active=1;block_fetch=0;bitmap_start();bitmap_pixels(0,&index);ready();
    assert(bitmap_pixels(1000,&index));bitmap_stop();
    puts("Bitmap worker: prefetch, ownership, single failure, cancellation, geometry and offline path passed");
}

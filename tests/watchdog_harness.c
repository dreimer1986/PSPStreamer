#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int SceUID;
typedef unsigned int SceSize;
#define PSP_O_WRONLY 1
#define PSP_O_CREAT 2
#define PSP_O_TRUNC 4
#define PSP_O_APPEND 8
static int playback_position_ms,playback_paused,timed_playing,tvout_video_active;
static int timed_running,timed_eof,timed_error,timed_audio_done,audio_running,audio_state;
static int audio_blocks_published,audio_played_blocks,timed_reader_id,audio_output_thread_id,remote_control_thread_id;
static int remote_http_last_result,remote_control_sequence,audio_start=1;
static unsigned int remote_http_attempts,remote_http_completed;
static const char *remote_http_stage="idle";
static struct {unsigned int write,read;} timed_video,timed_audio;
static const char *h264_hw_last_step(void) { return "idle"; }
static unsigned long long clock_us;
static unsigned long long sceKernelGetSystemTimeWide(void) { return clock_us; }
static int fail_open,fail_write,fail_thread,healthy,closes;
static char contents[10000],path_seen[100];
static int sceIoOpen(const char *path,int flags,int mode) {
    (void)mode; strcpy(path_seen,path);
    if(flags&PSP_O_TRUNC) contents[0]=0;
    return fail_open?-22:5;
}
static int sceIoWrite(int fd,const char *text,int n) {
    assert(fd==5); if(fail_write) return n-1;
    assert(strlen(contents)+n<sizeof(contents)); strncat(contents,text,n); return n;
}
static int sceIoClose(int fd) { assert(fd==5); closes++; return 0; }
static int sceIoMkdir(const char *path,int mode) { (void)path; (void)mode; return 0; }
static void sceKernelDelayThread(int us);
static int (*worker_fn)(SceSize,void *);
static int sceKernelCreateThread(const char *name,int (*fn)(SceSize,void *),int p,int stack,int flags,void *opt) {
    (void)name;(void)p;(void)stack;(void)flags;(void)opt; worker_fn=fn; return fail_thread?-23:9;
}
static int sceKernelStartThread(int id,int args,void *p) { assert(id==9); return worker_fn(args,p); }
static void sceKernelDeleteThread(int id) { assert(id==9); }
static void sceKernelWaitThreadEnd(int id,void *p) { assert(id==9 && !p); }
#include "video_watchdog.h"
static void sceKernelDelayThread(int us) {
    clock_us+=us;
    if(healthy) { video_watch_ping("healthy"); playback_position_ms++; audio_played_blocks++; remote_http_completed++; }
    if(clock_us>=10000000) video_watch_running=0;
}
int main(void) {
    fail_open=1; assert(video_watch_start(1)==-22 && video_watch_id==-1);
    fail_open=0; fail_write=1; assert(video_watch_start(1)<0 && closes==1);
    fail_write=0; fail_thread=1; assert(video_watch_start(1)==-23);
    assert(strstr(contents,"monitor thread failed"));
    fail_thread=0; healthy=1;
    assert(video_watch_start(1)==0); video_watch_stop();
    assert(!strcmp(path_seen,"ms0:/PSP/SYSTEM/PSPStreamer-watch-music.txt"));
    assert(strstr(contents,"monitor running") && strstr(contents,"monitor stopped"));
    assert(!strstr(contents,"heartbeat_age_ms="));
    healthy=0; clock_us=0; timed_playing=1;
    assert(video_watch_start(0)==0); video_watch_stop();
    assert(!strcmp(path_seen,"ms0:/PSP/SYSTEM/PSPStreamer-watch-video.txt"));
    assert(strstr(contents,"heartbeat_age_ms=8000") && strstr(contents,"remote: phase="));
    clock_us=0;
    assert(video_watch_start(1)==0); video_watch_stop();
    assert(strstr(contents,"progress_age_ms=8000"));
    return 0;
}

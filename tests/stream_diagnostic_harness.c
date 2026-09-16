#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
typedef int SceUID;
#define PSP_O_WRONLY 1
#define PSP_O_CREAT 2
#define PSP_O_TRUNC 4
#define SCE_NET_INET_POLLIN 1
struct SceNetInetPollfd { int fd,events,revents; };
static int timed_socket=3,timed_running=1,timed_playing=1,playback_paused;
static int mode,reads,fail_write;
static int server_https;
static int tls_recv(int fd,void *data,int size,int timeout) {(void)fd;(void)data;(void)size;(void)timeout;return -1;}
static unsigned long long tick;
static char report[1024];
static unsigned long long sceKernelGetSystemTimeWide(void) { return tick; }
static int sceNetInetGetErrno(void) { return 104; }
static int sceNetInetPoll(struct SceNetInetPollfd *p,int n,int timeout) {
    assert(n==1 && timeout==100); tick+=100000;
    p->revents=1;
    if(mode==6 && tick>100000 && tick<8100000) return 0;
    if(mode==7) { if(tick>=700000) timed_running=0; return 0; }
    return mode==1?0:mode==2?-1:1;
}
static int sceNetInetGetsockopt(int fd,int level,int opt,void *v,socklen_t *size) {
    (void)fd;(void)level;(void)opt;(void)size; *(int *)v=104; return 0;
}
static int sceNetInetRecv(int fd,void *data,int size,int flags) {
    (void)fd;(void)flags;
    if(mode==3) return -1;
    if(mode==4 || (mode==5 && reads++)) return 0;
    assert(size>0); *(char *)data='x'; return 1;
}
static int sceIoOpen(const char *path,int flags,int mode_) {
    (void)flags;(void)mode_;
    assert(!strcmp(path,"ms0:/PSP/SYSTEM/PSPStreamer-stream-error.txt")); return 8;
}
static int sceIoWrite(int fd,const char *text,int size) {
    assert(fd==8 && size<(int)sizeof(report)); memcpy(report,text,size);report[size]=0;
    return fail_write?size-1:size;
}
static int sceIoClose(int fd) { assert(fd==8);return 0; }
#include "stream_diagnostic.h"
/* TIMED_READ */
int main(void) {
    unsigned char data[4];
    stream_diag_reset(); assert(timed_read(data,4)==1 && stream_diag.bytes==4);
    mode=1; tick=0; stream_diag_reset();
    assert(timed_read(data,4)==-1 && tick==30000000);
    assert(!strcmp(stream_diag.reason,"read inactivity timeout"));
    timed_playing=0; tick=0;
    assert(timed_read(data,4)==-1 && tick==180000000);
    timed_playing=1; mode=6; tick=0; stream_diag_reset();
    assert(timed_read(data,4)==1 && tick==8300000);
    assert(stream_diag.received==4 && !memcmp(data,"xxxx",4));
    mode=7; tick=0;
    assert(timed_read(data,4)==-1 && tick==700000);
    timed_running=1;
    mode=2; stream_diag_reset(); assert(timed_read(data,4)==0);
    assert(stream_diag.socket_error==104 && !strcmp(stream_diag.reason,"poll error"));
    mode=3; stream_diag_reset(); assert(timed_read(data,4)==0);
    assert(stream_diag.socket_error==104 && !strcmp(stream_diag.reason,"recv error"));
    mode=4; stream_diag_reset(); assert(timed_read(data,4)==0);
    assert(!strcmp(stream_diag.reason,"TCP EOF"));
    mode=5; stream_diag_reset(); assert(timed_read(data,4)==-1);
    assert(stream_diag.received==1 && stream_diag.wanted==4);
    assert(stream_diag_save(-1320)==0 && strstr(report,"result=-1320"));
    assert(strstr(report,"reason=TCP EOF") && strstr(report,"received=1"));
    fail_write=1; assert(stream_diag_save(-1320)<0);
    return 0;
}

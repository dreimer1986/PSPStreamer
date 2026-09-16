/* Run the actual PSP FLV reader with local I/O and fragmented reads. Any
 * network call fails the test, including on EOF. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <netinet/in.h>
#include "flv.h"
#define DEBUG_DIAG(statement) do { statement } while(0)
typedef int SceSize;
typedef long long SceOff;
#define PSP_O_RDONLY 0
#define PSP_SEEK_SET SEEK_SET
static int offline_active=1,offline_reader_fd=-1,timed_running=1,timed_playing=1;
static int timed_socket=-1,timed_has_audio,timed_error,timed_eof,playback_paused,server_https;
static const char * volatile timed_error_step;
static int timed_video,timed_audio;
static unsigned int offline_seek_offset;
static char offline_movie[512],timed_request[4];
static FILE *source,*video,*audio;
static int video_pts=-1,audio_pts=-1,frames,samples,fail_read;
static struct {const char *reason,*stage;int wanted,received,socket_error,ap_state,ap_result,audio_pts,video_pts;unsigned int failure_ms;} stream_diag;
static unsigned long long sceKernelGetSystemTimeWide(void){return 1;}
static int sceIoOpen(const char *path,int flags,int mode){(void)flags;(void)mode;source=fopen(path,"rb");return source?1:-77;}
static int sceIoRead(int fd,void *out,int size){assert(fd==1);if(fail_read)return -78;if(size>7)size=7;return fread(out,1,size,source);}
static SceOff sceIoLseek(int fd,SceOff offset,int where){assert(fd==1);return fseek(source,offset,where)?-1:offset;}
static int sceIoClose(int fd){assert(fd==1);return fclose(source);}
static int timed_recv(unsigned char *p,int n){(void)p;(void)n;assert(0);return -1;}
static void stream_diag_reset(void){memset(&stream_diag,0,sizeof(stream_diag));}
static int sceNetInetSocket(int a,int b,int c){(void)a;(void)b;(void)c;assert(0);return -1;}
static int sceNetInetGetErrno(void){assert(0);return -1;}
static int prepare_server(struct sockaddr_in *p){(void)p;assert(0);return -1;}
static int timed_connect(int fd,struct sockaddr_in *p){(void)fd;(void)p;assert(0);return -1;}
static int tls_send(int a,void*b,int c,int*d,int e){(void)a;(void)b;(void)c;(void)d;(void)e;assert(0);return -1;}
static int sceNetInetSend(int a,void*b,int c,int d){(void)a;(void)b;(void)c;(void)d;assert(0);return -1;}
static int connection_close(int fd){(void)fd;assert(0);return -1;}
static int sceNetApctlGetState(int *s){(void)s;assert(0);return -1;}
static int timed_put(int *q,const unsigned char *data,int size,int pts) {
    if(q==&timed_video){assert(pts>video_pts);video_pts=pts;frames++;assert(fwrite(data,1,size,video)==(size_t)size);}
    else {assert(q==&timed_audio);assert(pts>audio_pts);audio_pts=pts;samples++;assert(fwrite(data,1,size,audio)==(size_t)size);}
    return 0;
}
/* TIMED_READ */
/* TIMED_READER */
int main(int argc,char **argv) {
    assert(argc==5);
    snprintf(offline_movie,sizeof(offline_movie),"%s/missing.flv",argv[1]);
    assert(timed_reader(0,NULL)==0 && timed_error==-77 && timed_eof);
    assert(!strcmp(timed_error_step,"Open local video"));
    timed_error=timed_eof=0;
    offline_reader_fd=1;fail_read=1;
    unsigned char byte;
    assert(timed_read(&byte,1)==-1 && timed_error==-78);
    assert(!strcmp(timed_error_step,"Read local video"));
    offline_reader_fd=-1;fail_read=timed_error=0;
    strcpy(offline_movie,argv[1]);offline_seek_offset=strtoul(argv[2],NULL,10);
    video=fopen(argv[3],"wb");audio=fopen(argv[4],"wb");assert(video&&audio);
    assert(timed_reader(0,NULL)==0);assert(timed_eof&&!timed_error&&frames>0&&samples>0);
    assert(offline_reader_fd==-1&&timed_socket==-1);
    fclose(video);fclose(audio);
    printf("%d %d %d %d\n",frames,samples,video_pts,audio_pts);
    return 0;
}

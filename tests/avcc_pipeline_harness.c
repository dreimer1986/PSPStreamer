#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include "flv.h"
#define DEBUG_DIAG(x) do {} while(0)
typedef int SceUID;
typedef unsigned SceUInt;
typedef intptr_t SceInt32;
typedef struct {int unused;} SceMpeg;
typedef struct {int unused;} SceMpegAu;
typedef struct {int unused;} SceMpegRingbuffer;
static int semas[8],sema_next=1,timed_running=1,timed_video_blocked;
static int sceKernelCreateSema(const char*n,int a,int count,int max,void*p) {
    (void)n;(void)a;(void)max;(void)p;semas[sema_next]=count;return sema_next++;
}
static int sceKernelDeleteSema(int s){semas[s]=0;return 0;}
static int sceKernelSignalSema(int s,int count){semas[s]+=count;return 0;}
static int sceKernelWaitSema(int s,int count,SceUInt*t) {
    (void)t;if(semas[s]<count){timed_running=0;return -1;}semas[s]-=count;return 0;
}
/* QUEUE_TYPES */
static TimedQueue timed_video;
/* QUEUE_FUNCTIONS */
/* VIDEO_PUT */
static const AvcPacket *expected;
static int stage,expected_mode,expected_stride,expected_height,fail_decode,finish_count;
static int sceMpegInit(void){return 0;}
static int sceMpegQueryMemSize(int mode){assert(mode==4 || mode==5);return 64;}
static int sceMpegCreate(SceMpeg*m,void*s,int size,SceMpegRingbuffer*r,int stride,int mode,SceInt32 workspace) {
    (void)m;(void)s;(void)r;(void)mode;assert(size==64 && stride==512 && workspace);return 0;
}
static int sceMpegInitAu(SceMpeg*m,void*w,SceMpegAu*a){(void)m;(void)a;assert(w);return 0;}
static int sceMpegDelete(SceMpeg*m){(void)m;return 0;}
static int sceMpegFinish(void){finish_count++;return 0;}
static void sceKernelDcacheWritebackInvalidateAll(void){assert(stage==0);stage=1;}
static void sceKernelDcacheWritebackInvalidateRange(void*p,int n){assert(p && n==expected_stride*expected_height*4 && stage==4);stage=5;}
static void sceKernelDcacheInvalidateRange(void*p,int n){assert(p && n==expected_stride*expected_height*4 && stage==6);stage=7;}
static int sceMpegAvcDecode(SceMpeg*m,SceMpegAu*a,int stride,void*p,SceInt32*pictures) {
    (void)m;(void)a;assert(!p && stride==expected_stride && stage==2);stage=3;
    *pictures=1;return fail_decode?-123:0;
}
/* HARDWARE_DECODER */
int sceMpegGetAvcNalAu(SceMpeg*m,AvcNalInput*i,SceMpegAu*a) {
    (void)m;(void)a;assert(stage==1);stage=2;
    assert(i->data==expected->data && !((uintptr_t)i->data&63));
    assert(i->data_size==(int)expected->data_size && i->nal_length_bytes==4 && i->mode==expected_mode);
    assert(i->sps==expected->config.sps && i->pps==expected->config.pps);
    assert(i->sps_size==2 && i->pps_size==2);return 0;
}
int sceMpegAvcDecodeDetail2(SceMpeg*m,AvcDetail**d) {
    (void)m;assert(stage==3);stage=4;
    static AvcPictureInfo picture;static AvcYuvInfo yuv;static AvcDetail detail;
    picture.width=expected_stride==512?480:720;picture.height=expected_height;
    detail.picture=&picture;detail.yuv=&yuv;*d=&detail;return 0;
}
int sceMpegBaseCscAvc(void*p,int zero,int stride,AvcCsc*c) {
    assert(p && !zero && stride==expected_stride && stage==5);stage=6;
    assert(c->height_blocks==(expected_height+15)/16);return 0;
}
int main(void) {
    const unsigned char raw[]={0,0,0,4,0x65,1,2,3};
    AvcConfig config={.sps={0x67,42},.pps={0x68,17},.sps_size=2,.pps_size=2,.length_size=4};
    for(int tv=0;tv<2;tv++) {
        assert(!timed_queue_init(&timed_video));
        unsigned char body[sizeof(raw)];memcpy(body,raw,sizeof(raw));
        assert(!timed_put_video(&config,body,sizeof(body),125));
        config.sps[1]=99;memset(body,0,sizeof(body));
        TimedPacket packet={0};assert(timed_get(&timed_video,&packet));
        expected=(AvcPacket *)packet.data;
        assert(packet.pts==125 && expected->config.sps[1]==42);
        assert(!memcmp(expected->data,raw,sizeof(raw)));
        expected_stride=tv?768:512;expected_height=tv?480:272;
        h264_hw_set_output_layout(expected_stride,expected_height,tv?5:4);
        assert(!h264_hw_init_avcc(expected,packet.size));
        void *frame=memalign(64,expected_stride*expected_height*4);assert(frame);
        stage=0;expected_mode=3;fail_decode=1;
        assert(h264_hw_decode_avcc(expected,packet.size,frame)==-123 && stage==3);
        stage=0;fail_decode=0;
        assert(h264_hw_decode_avcc(expected,packet.size,frame)==1 && stage==7);
        stage=0;expected_mode=0;
        assert(h264_hw_decode_avcc(expected,packet.size,NULL)==1 && stage==3);
        stage=0;assert(h264_hw_decode_avcc(expected,packet.size-1,frame)<0 && stage==0);
        h264_hw_shutdown();free(frame);free(packet.data);
        config.sps[1]=42;
        /* Ring wrap, producer ownership, queued teardown and cancellation. */
        for(int k=0;k<300;k++) {
            assert(!timed_put_video(&config,raw,sizeof(raw),k));
            assert(timed_get(&timed_video,&packet) && packet.pts==k);free(packet.data);
        }
        for(int k=0;k<128;k++)assert(!timed_put_video(&config,raw,sizeof(raw),k));
        assert(timed_put_video(&config,raw,sizeof(raw),129)<0 && !timed_video_blocked);
        timed_queue_destroy(&timed_video);timed_running=1;
    }
    assert(finish_count==2);
    puts("AVCC pipeline: owned queue, wrap/cancel/teardown, LCD/TV direct firmware pointers, modes and cache barriers OK");
}

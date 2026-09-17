#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "flv.h"
#include "mp3_preroll.h"
typedef int SceUID;
typedef long long SceOff;
#define PSP_SEEK_SET 0
static int timed_running=1,position,used=13;
static unsigned char file[16384];
static SceOff sceIoLseek(int fd,SceOff at,int mode) {
    assert(fd==1 && mode==0);
    if(at<0 || at>used)return -1;
    position=at;return at;
}
static int sceIoRead(int fd,void *out,int size) {
    assert(fd==1);
    if(size>3)size=3;
    if(size>used-position)size=used-position;
    memcpy(out,file+position,size);position+=size;return size;
}
#include "offline_preroll.h"
static void put32(unsigned char *p,unsigned int n) {p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;}
static int append(int type) {
    int at=used,size=type==8?105:200;
    unsigned char *p=file+at;memset(p,0,size+15);p[0]=type;
    p[1]=size>>16;p[2]=size>>8;p[3]=size;
    if(type==8) {
        p[11]=0x2f;p[12]=0xff;p[13]=0xfb;p[14]=0x10;p[15]=0;
        p[16]=0xff;p[17]=0x80; /* Max reservoir dependency, 32-kbit frame. */
    }
    put32(p+11+size,size+11);used+=size+15;return at;
}
int main(void) {
    unsigned int start,target;
    int offsets[12];
    for(int i=0;i<12;i++){offsets[i]=append(8);append(9);}
    target=used;
    assert(!offline_preroll_start(1,target,&start));
    assert(start==(unsigned int)offsets[2]); /* 8 * 68 >= 511 + two frames. */
    Mp3Preroll state={0};
    int ready=0;
    for(int i=2;i<12;i++) {
        int missing=mp3_preroll_missing(&state,file+offsets[i]+12,104);
        if(i<10)assert(missing==1);else {assert(!missing);ready++;}
    }
    assert(ready==2 && state.bytes==511);
    assert(!offline_preroll_start(1,offsets[1],&start)&&start==13);
    assert(!offline_preroll_start(1,13,&start)&&start==13);
    assert(offline_preroll_start(1,12,&start)<0);
    timed_running=0;assert(offline_preroll_start(1,target,&start)<0);timed_running=1;
    put32(file+target-4,0xffffffff);assert(offline_preroll_start(1,target,&start)<0);
    put32(file+target-4,0);assert(offline_preroll_start(1,target,&start)<0);
    unsigned char crc[104]={255,250,0x10,0,0,0,255,128};int back,bytes;
    assert(!mp3_reservoir_info(crc,sizeof(crc),&back,&bytes)&&back==511&&bytes==66);
    crc[3]=0xc0;assert(!mp3_reservoir_info(crc,sizeof(crc),&back,&bytes)&&bytes==81);
    assert(mp3_reservoir_info(crc,7,&back,&bytes)<0);
    crc[0]=0;assert(mp3_reservoir_info(crc,sizeof(crc),&back,&bytes)<0);
    return 0;
}

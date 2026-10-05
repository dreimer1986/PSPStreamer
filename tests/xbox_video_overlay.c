/* Host tests for the actual packing and PVIDEO ownership code. Hardware
 * boundaries are stubbed; this cannot validate the TV encoder or scanout. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t Uint32;
typedef struct {int x,y,w,h;} SDL_Rect;
typedef struct {unsigned width;uint8_t *data;} Plane;
typedef struct {unsigned width,height;Plane y,cb,cr;} plm_frame_t;
static uint32_t registers[0xc00/4];
static unsigned ticks=1,allocations,frees;
static int allocation_fail,stop_stuck;
static Uint32 SDL_GetTicks(void){return ticks;}
static uint32_t pmc=0x03111113;
static void pmc_write(uint32_t value){if(stop_stuck)return;pmc=value;if(!(value&(1u<<28)))registers[0x700/4]=0;}
#define XBOX_PMC_REGISTER (&pmc)
#define XBOX_PMC_WRITE(value) pmc_write(value)
#define PAGE_READWRITE 1
#define PAGE_WRITECOMBINE 2
#define XBOX_VIDEO_REGISTER(offset) (&registers[(offset)/4])
static void *MmAllocateContiguousMemoryEx(unsigned size,unsigned low,unsigned high,unsigned align,unsigned flags){
    assert(low==0&&high==0x03ffffff&&align==64&&flags==3);
    if(allocation_fail)return NULL;
    void *p=NULL;assert(!posix_memalign(&p,64,size));allocations++;return p;
}
static void MmFreeContiguousMemory(void *p){assert(!(registers[0x700/4]&0x11));frees++;free(p);}
static uintptr_t MmGetPhysicalAddress(void *p){return (uintptr_t)p&0x03ffffff;}
#include "../xbox-client/video_overlay.h"
static void packing(unsigned w,unsigned h){
    unsigned ys=w+17,us=w/2+11,vs=w/2+19,pitch=(w*2+63)&~63u;
    uint8_t *y=malloc(ys*h),*u=malloc(us*(h/2)),*v=malloc(vs*(h/2)),*out;
    assert(!posix_memalign((void **)&out,64,pitch*h+64));
    for(unsigned i=0;i<ys*h;i++)y[i]=(i*17+13)&255;
    for(unsigned i=0;i<us*h/2;i++)u[i]=(i*3+47)&255;
    for(unsigned i=0;i<vs*h/2;i++)v[i]=(i*7+129)&255;
    memset(out,0xa5,pitch*h+64);
    xbox_pack_yuy2(out,pitch,y,ys,u,us,v,vs,w,h);
    for(unsigned row=0;row<h;row++){
        for(unsigned x=0;x<w;x+=2){
            unsigned k=row*pitch+x*2;
            assert(out[k]==y[row*ys+x]&&out[k+2]==y[row*ys+x+1]);
            assert(out[k+1]==u[(row/2)*us+x/2]&&out[k+3]==v[(row/2)*vs+x/2]);
        }
        for(unsigned x=w*2;x<pitch;x++)assert(out[row*pitch+x]==0xa5);
    }
    for(unsigned x=pitch*h;x<pitch*h+64;x++)assert(out[x]==0xa5);
    /* EMMS must have restored floating point use after the MMX packer. */
    volatile double a=1.25,b=2.5;assert(a+b==3.75);
    free(y);free(u);free(v);free(out);
}
int main(void){
    const unsigned sizes[][2]={{2,2},{10,6},{480,272},{640,360},{720,480},{720,576},{1280,720},{1920,1080}};
    for(unsigned i=0;i<sizeof(sizes)/sizeof(*sizes);i++)packing(sizes[i][0],sizes[i][1]);
    uint8_t y[64]={0},u[16]={0},v[16]={0};plm_frame_t f={8,8,{8,y},{4,u},{4,v}};SDL_Rect dst={12,20,640,360};
    registers[0x700/4]=0x11; /* inherited busy slots must not block first frame */
    assert(overlay_prepare(8,8));assert(allocations==1);
    assert(overlay.initial_buffer==0x11&&overlay.reset_buffer==0);
    assert(pmc==0x13111113&&registers[0x704/4]==0);
    assert(overlay_present(&f,dst)==1);assert(overlay.slot==1&&overlay.shown==1);
    assert(registers[0x958/4]==(64|(1u<<16)|(1u<<20)));
    assert(registers[0x948/4]==(20u<<16|12));
    assert(registers[0x938/4]==(7u<<20)/639);
    assert(overlay_present(&f,dst)==1);assert(overlay.slot==0);
    registers[0x700/4]=1;assert(overlay_present(&f,dst)==0);assert(overlay.slot==0);
    ticks+=251;assert(overlay_present(&f,dst)==-1);assert(overlay.disabled);
    overlay_close();assert(frees==1&&!overlay.memory&&pmc==0x03111113);
    pmc|=1u<<28;
    assert(overlay_prepare(8,8));assert(allocations==2);
    registers[0x700/4]=1;stop_stuck=1;overlay_close();assert(overlay.disabled&&overlay.memory&&frees==1);
    stop_stuck=0;overlay_close();assert(!overlay.memory&&frees==2&&pmc==0x13111113);
    allocation_fail=1;assert(!overlay_prepare(8,8));assert(overlay.disabled);overlay_close();allocation_fail=0;
    assert(!overlay_prepare(7,8));overlay_close();assert(!overlay_prepare(1922,1080));overlay_close();
    video_hardware=0;assert(!overlay_prepare(8,8));assert(allocations==frees);
    puts("PASS: YUY2 (8 sizes, padded planes, guards), register layout, double buffering, busy/stop timeout, allocation failure and software selection");
}

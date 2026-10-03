#include <assert.h>
#include <stdio.h>
#include "../psp-controller/health_rumble.h"
static uint32_t health=100,pointer=0x08801000,gate=1;
static int reads,fail;
static int read_memory(uint32_t a,unsigned size,uint32_t *out,void *ctx){
    (void)ctx;reads++;assert(hr_range(a,size));if(fail)return 0;
    if(a==0x08800000)*out=pointer;
    else if(a==0x08802000)*out=gate;
    else if(a==0x08801000)*out=health;
    else return 0;
    return 1;
}
static uint64_t now=1000000;
static unsigned step(HealthRumble *s,int *c,int active){now+=20000;return hr_step(s,c,now,active,read_memory,NULL);}
static void settle(HealthRumble *s,int *c){for(int i=0;i<26;i++)assert(!step(s,c,1));}
int main(void){
    HealthRumble s={0};int c[HR_COUNT]={0,0x08801000,2,0,0,0,100,180,120,200,0,1};
    assert(!step(&s,c,1)&&reads==0);c[0]=1;settle(&s,c);
    health=90;assert(step(&s,c,1)==180&&s.events==1);
    int old=reads;assert(hr_step(&s,c,now+1000,1,read_memory,NULL)==180&&reads==old);
    health=80;assert(step(&s,c,1)==180&&s.events==1); /* cooldown */
    for(int i=0;i<10;i++)step(&s,c,1);
    assert(!step(&s,c,1));health=70;assert(step(&s,c,1)==180&&s.events==2);
    health=65535;assert(!step(&s,c,1)&&!s.baseline); /* invalid cancels immediately */
    health=60;settle(&s,c);health=0;assert(step(&s,c,1)==180); /* fatal hit */
    assert(!step(&s,c,0)&&!s.baseline);health=100;settle(&s,c);
    now+=1000000;health=50;assert(!step(&s,c,1)); /* missing samples rebase */
    settle(&s,c);health=100;assert(!step(&s,c,1));health=90;assert(!step(&s,c,1));
    settle(&s,c);health=80;assert(step(&s,c,1)==180);
    c[HR_GATE]=0x08802000;gate=0;assert(!step(&s,c,1)&&!s.baseline);
    gate=1;settle(&s,c);health=70;assert(step(&s,c,1)==180);
    c[HR_POINTER]=1;c[HR_ADDRESS]=0x08800000;
    assert(!step(&s,c,0));settle(&s,c);health=60;assert(step(&s,c,1)==180);
    pointer=0x88001000;assert(!step(&s,c,1)&&!s.baseline); /* kernel alias rejected */
    pointer=0xffffffff;c[HR_OFFSET]=4;assert(!step(&s,c,1));
    pointer=0x08800ffc;settle(&s,c);health=50;assert(step(&s,c,1)==180);
    c[HR_POINTER]=0;c[HR_OFFSET]=0;c[HR_ADDRESS]=0x08801000;c[HR_GATE]=0;c[HR_TYPE]=4;c[HR_MAX]=1;
    assert(!step(&s,c,0));health=0x3f800000;settle(&s,c);health=0x3f000000;
    assert(step(&s,c,1)==180);health=0x7fc00000;assert(!step(&s,c,1));
    health=0x7f800000;assert(!step(&s,c,1));health=0xbf000000;assert(!step(&s,c,1));
    assert(hr_float_bound(100)==0x42c80000);assert(hr_float_bound(0)==0);
    c[HR_ADDRESS]=0x08801001;old=reads;assert(!step(&s,c,1)&&reads==old);
    c[HR_ADDRESS]=0x087fffff;assert(!step(&s,c,1));
    c[HR_ADDRESS]=0x0a000000;assert(!step(&s,c,1));
    c[HR_ADDRESS]=0x08801000;fail=1;assert(!step(&s,c,1));fail=0;
    for(int type=1;type<=3;type++){
        c[HR_TYPE]=type;c[HR_MAX]=100;health=100;assert(!step(&s,c,0));settle(&s,c);
        health=99;assert(step(&s,c,1)==180);
    }
    puts("Health rumble: types, validity, pointer/gate, cooldown, expiry, re-arm and no-read-disabled OK");
}

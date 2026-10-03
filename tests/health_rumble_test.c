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
    c[HR_DYNAMIC]=1;c[HR_PEAK]=120;c[HR_DURATION_MIN]=60;c[HR_DURATION_MAX]=500;c[HR_STRENGTH]=255;
    const unsigned hits[]={1,25,60,90,120,140,240};
    const unsigned powers[]={38,64,153,217,255,255,255};
    const unsigned times[]={60,100,220,350,500,500,500};
    for(unsigned i=0;i<sizeof(hits)/sizeof(*hits);i++){
        unsigned power,ms;hr_curve((uint64_t)hits[i]<<16,c,&power,&ms);
        assert(power==powers[i]&&ms==times[i]);
    }
    assert(hr_health_q16(0x43700000,4)==(240u<<16));
    assert(hr_health_q16(0x3f000000,4)==32768);
    assert(hr_health_q16(1,4)==0);
    assert(hr_health_q16(0x4effffff,4)==((uint64_t)2147483520u<<16));
    c[HR_MAX]=240;c[HR_TYPE]=4;health=hr_float_bound(240);assert(!step(&s,c,0));settle(&s,c);
    health=hr_float_bound(215);assert(step(&s,c,1)==64&&s.damage==(25u<<16));
    assert(s.effect_until==now+100000);
    health=hr_float_bound(125);assert(step(&s,c,1)==217&&s.events==2); /* stronger bypasses cooldown */
    uint64_t end=s.effect_until;
    health=hr_float_bound(120);assert(step(&s,c,1)==217&&s.events==2&&s.effect_until==end);
    for(int i=0;i<17;i++)step(&s,c,1);
    assert(!step(&s,c,1));health=0;assert(step(&s,c,1)==255&&s.damage==(120u<<16));
    assert(s.effect_until==now+500000);
    assert(!step(&s,c,0));health=hr_float_bound(240);settle(&s,c);
    c[HR_DURATION_MIN]=600;health=hr_float_bound(200);assert(!step(&s,c,1)&&!s.baseline);
    c[HR_DURATION_MIN]=60;c[HR_STRENGTH]=0;health=hr_float_bound(240);settle(&s,c);
    health=hr_float_bound(120);assert(!step(&s,c,1));
    c[HR_STRENGTH]=180;unsigned power,ms;hr_curve(120u<<16,c,&power,&ms);assert(power==180&&ms==500);
    c[HR_PEAK]=240;hr_curve(120u<<16,c,&power,&ms);assert(power==108&&ms==220);
    c[HR_PEAK]=120;unsigned last_power=0,last_ms=0;
    for(unsigned d=1;d<=360;d++){
        hr_curve((uint64_t)d<<16,c,&power,&ms);
        assert(power>=last_power&&power<=180&&ms>=last_ms&&ms<=500);
        last_power=power;last_ms=ms;
    }
    hr_curve((uint64_t)2147483647u<<16,c,&power,&ms);assert(power==180&&ms==500);
    puts("Health rumble: fixed/dynamic modes, integer-only float damage, 120 HP cap, stronger-hit upgrade and expiry OK");
}

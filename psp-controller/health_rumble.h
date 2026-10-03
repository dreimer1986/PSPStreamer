/* Read-only, opt-in game health events. No game code patches or allocations. */
#ifndef HEALTH_RUMBLE_H
#define HEALTH_RUMBLE_H
#include <stdint.h>
#include <string.h>
enum { HR_ENABLED,HR_ADDRESS,HR_TYPE,HR_POINTER,HR_OFFSET,HR_MIN,HR_MAX,
       HR_STRENGTH,HR_DURATION,HR_COOLDOWN,HR_GATE,HR_GATE_VALUE,HR_COUNT };
/* Unsigned 8/16/32-bit, or IEEE float32. Addresses are PSP virtual addresses,
 * not CWCheat offsets. The conservative user window also fits a PSP-1000. */
static int hr_range(uint32_t a,unsigned n) {
    return a>=0x08800000u && a<=0x0a000000u-n && !(a&(n-1));
}
typedef struct {
    uint32_t address,previous;
    uint64_t last_sample,settle_until,cooldown_until,effect_until;
    int baseline;
    unsigned events;
} HealthRumble;
typedef int (*HealthRead)(uint32_t,unsigned,uint32_t *,void *);
/* Positive integer -> float bits, rounding down. No FPU use in a kernel
 * controller thread (and never execute arithmetic on untrusted float data). */
static uint32_t hr_float_bound(uint32_t n) {
    if(!n)return 0;
    unsigned e=0;for(uint32_t v=n;v>1;v>>=1)e++;
    uint32_t m=e>23?n>>(e-23):n<<(23-e);
    return ((e+127)<<23)|(m&0x7fffff);
}
static unsigned hr_step(HealthRumble *s,const int *c,uint64_t now,int active,
                        HealthRead read,void *ctx) {
    if(!active||!c[HR_ENABLED]){memset(s,0,sizeof(*s));return 0;}
    if(s->last_sample && now>=s->last_sample && now-s->last_sample<20000)
        return now<s->effect_until?(unsigned)c[HR_STRENGTH]:0;
    int gap=s->last_sample && (now<s->last_sample || now-s->last_sample>250000);
    s->last_sample=now;
    uint32_t a=(uint32_t)c[HR_ADDRESS],raw=0;
    unsigned bytes=c[HR_TYPE]==1?1:c[HR_TYPE]==2?2:4;
    if(c[HR_GATE]){
        if(!hr_range(c[HR_GATE],4)||!read(c[HR_GATE],4,&raw,ctx)||raw!=(uint32_t)c[HR_GATE_VALUE])goto invalid;
    }
    if(c[HR_POINTER]){
        if(!hr_range(a,4)||!read(a,4,&raw,ctx)||raw>UINT32_MAX-(unsigned)c[HR_OFFSET])goto invalid;
        a=raw+(unsigned)c[HR_OFFSET];
    }else if(c[HR_OFFSET])goto invalid;
    if(c[HR_TYPE]<1||c[HR_TYPE]>4||c[HR_MIN]>=c[HR_MAX]||!hr_range(a,bytes)||!read(a,bytes,&raw,ctx))goto invalid;
    uint32_t value=raw,minimum=c[HR_MIN],maximum=c[HR_MAX];
    if(c[HR_TYPE]==4){
        if(raw>=0x7f800000u)goto invalid; /* negative, infinity or NaN */
        minimum=hr_float_bound(minimum);maximum=hr_float_bound(maximum);
    }
    if(value<minimum||value>maximum)goto invalid;
    if(!s->baseline||s->address!=a||gap||value>s->previous){
        s->baseline=1;s->address=a;s->previous=value;s->settle_until=now+500000;s->effect_until=0;return 0;
    }
    if(value<s->previous && now>=s->settle_until && now>=s->cooldown_until){
        s->events++;
        s->effect_until=now+(uint64_t)c[HR_DURATION]*1000;
        s->cooldown_until=now+(uint64_t)c[HR_COOLDOWN]*1000;
    }
    s->previous=value;
    return now<s->effect_until?(unsigned)c[HR_STRENGTH]:0;
invalid:
    s->baseline=0;s->effect_until=0;return 0;
}
#endif

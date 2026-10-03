/* Read-only, opt-in game health events. No game code patches or allocations. */
#ifndef HEALTH_RUMBLE_H
#define HEALTH_RUMBLE_H
#include <stdint.h>
#include <string.h>
enum { HR_ENABLED,HR_ADDRESS,HR_TYPE,HR_POINTER,HR_OFFSET,HR_MIN,HR_MAX,
       HR_STRENGTH,HR_DURATION,HR_COOLDOWN,HR_GATE,HR_GATE_VALUE,
       HR_DYNAMIC,HR_PEAK,HR_DURATION_MIN,HR_DURATION_MAX,HR_COUNT };
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
    unsigned output;
    uint64_t damage;
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
/* Q16 health, including float32 fractions. Integer-only kernel arithmetic.
 * Called only after finite, nonnegative, <= INT_MAX range validation. */
static uint64_t hr_health_q16(uint32_t value,int type){
    if(type!=4)return (uint64_t)value<<16;
    int exponent=(int)((value>>23)&255);
    if(!exponent)return 0;
    uint64_t mantissa=(value&0x7fffff)|0x800000;
    int shift=exponent-134;
    return shift>=0?mantissa<<shift:shift<=-64?0:mantissa>>(-shift);
}
static void hr_curve(uint64_t damage,const int *c,unsigned *power,unsigned *ms){
    static const unsigned hp[]={0,1,25,60,90,120};
    static const unsigned strength[]={1500,1500,2500,6000,8500,10000};
    static const unsigned duration[]={0,0,40,160,290,440};
    uint64_t x=damage*120/(unsigned)c[HR_PEAK]; /* reference HP, Q16 */
    if(x>(120u<<16))x=120u<<16;
    unsigned i=1;while(i<5&&x>((uint64_t)hp[i]<<16))i++;
    uint64_t offset=x-((uint64_t)hp[i-1]<<16),span=(hp[i]-hp[i-1])<<16;
    unsigned level=strength[i-1]+(unsigned)((strength[i]-strength[i-1])*offset/span);
    unsigned time=duration[i-1]+(unsigned)((duration[i]-duration[i-1])*offset/span);
    *power=((unsigned)c[HR_STRENGTH]*level+5000)/10000;
    *ms=c[HR_DURATION_MIN]+((unsigned)(c[HR_DURATION_MAX]-c[HR_DURATION_MIN])*time+220)/440;
}
static unsigned hr_step(HealthRumble *s,const int *c,uint64_t now,int active,
                        HealthRead read,void *ctx) {
    if(!active||!c[HR_ENABLED]){memset(s,0,sizeof(*s));return 0;}
    if(s->last_sample && now>=s->last_sample && now-s->last_sample<20000)
        return now<s->effect_until?s->output:0;
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
    if(c[HR_DYNAMIC]&&(c[HR_PEAK]<1||c[HR_DURATION_MIN]<10||c[HR_DURATION_MAX]>1000||c[HR_DURATION_MIN]>c[HR_DURATION_MAX]))goto invalid;
    if(!s->baseline||s->address!=a||gap||value>s->previous){
        s->baseline=1;s->address=a;s->previous=value;s->settle_until=now+500000;s->effect_until=0;return 0;
    }
    if(value<s->previous && now>=s->settle_until){
        uint64_t damage=hr_health_q16(s->previous,c[HR_TYPE])-hr_health_q16(value,c[HR_TYPE]);
        int running=now<s->effect_until;
        int trigger=now>=s->cooldown_until;
        if(c[HR_DYNAMIC]&&running)trigger=damage>s->damage;
        if(trigger&&(!c[HR_DYNAMIC]||damage)){
            unsigned ms=c[HR_DURATION];s->output=c[HR_STRENGTH];
            if(c[HR_DYNAMIC])hr_curve(damage,c,&s->output,&ms);
            s->damage=damage;s->events++;
            s->effect_until=now+(uint64_t)ms*1000;
            s->cooldown_until=now+(uint64_t)c[HR_COOLDOWN]*1000;
        }
    }
    s->previous=value;
    return now<s->effect_until?s->output:0;
invalid:
    s->baseline=0;s->effect_until=0;return 0;
}
#endif

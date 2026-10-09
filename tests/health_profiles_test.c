#include <assert.h>
#include <stdio.h>
#include "../psp-controller/health_profiles.h"
static void line(HealthProfiles *p,const char *s) {
    char buf[384];snprintf(buf,sizeof(buf),"%s",s);
    assert(!health_profiles_line(p,buf,"ULES01298","ms0:/game"));
}
static unsigned hp[2]={100,100},gate=1;
static int read_hp(uint32_t a,unsigned n,uint32_t *v,void *ctx) {
    (void)n;(void)ctx;
    if(a==0x08802000){*v=gate;return 1;}
    if(a<0x08801000||a>0x08801004)return 0;
    *v=hp[(a-0x08801000)/4];return 1;
}
int main(void) {
    HealthProfiles p;health_profiles_init(&p);
    line(&p,"[path:ms0:/game|Fallback]");line(&p,"enabled=1");line(&p,"strength=99");
    line(&p,"[title:ULES01298|Player]");line(&p,"enabled=1");line(&p,"address=0x08801000");line(&p,"strength=100");
    line(&p,"[title:ULES-01298|Vehicle]");line(&p,"enabled=1");line(&p,"address=0x08801004");line(&p,"strength=230");
    line(&p,"[title:OTHER0000]");line(&p,"strength=255");health_profiles_finish(&p);
    assert(p.count==2 && p.rank==2 && p.config[0][HR_STRENGTH]==100 && p.config[1][HR_STRENGTH]==230);
    HealthRumble states[HR_PROFILE_MAX]={0};uint64_t t=1000000;
    for(int i=0;i<27;i++,t+=20000)assert(!health_profiles_step(&p,states,t,1,read_hp,NULL));
    hp[0]=90;assert(health_profiles_step(&p,states,t,1,read_hp,NULL)==100);t+=20000;
    hp[1]=70;assert(health_profiles_step(&p,states,t,1,read_hp,NULL)==230);
    assert(states[0].events==1&&states[1].events==1);
    p.config[1][HR_GATE]=0x08802000;gate=0;t+=20000;
    assert(health_profiles_step(&p,states,t,1,read_hp,NULL)==100&&!states[1].baseline);
    t+=20000;assert(!health_profiles_step(&p,states,t,0,read_hp,NULL)&&!states[0].baseline);
    health_profiles_init(&p);
    for(int i=0;i<17;i++)line(&p,"[title:ULES01298]");
    health_profiles_finish(&p);assert(p.count==16&&p.overflow);
    health_profiles_init(&p);
    line(&p,"[title:ULES01298]");line(&p,"enabled=1");health_profiles_finish(&p);
    assert(p.count==1&&p.config[0][HR_ENABLED]==1&&p.config[0][HR_MAX]==100);
    char bad[]="[title:ULES01298|]";assert(health_profiles_line(&p,bad,"","")<0);
    printf("Multi-rule parser/mixing/gates/legacy/limits OK; resident tables=%zu bytes\n",sizeof(p)+sizeof(states));
}

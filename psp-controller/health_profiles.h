/* Bounded multi-rule health profiles; OC/controller selection is unchanged. */
#ifndef HEALTH_PROFILES_H
#define HEALTH_PROFILES_H
#include "health_rumble.h"
#include "../psp-overclock/title_rules.h"
#define HR_PROFILE_MAX 16
static const TitleRuleKey health_keys[]={
    {"enabled",0,1},{"address",0,0x09ffffff},{"type",1,4},
    {"pointer",0,1},{"offset",0,65535},{"minimum",0,2147483647},
    {"maximum",1,2147483647},{"strength",0,255},{"duration_ms",10,1000},
    {"cooldown_ms",20,5000},{"gate_address",0,0x09ffffff},{"gate_value",0,2147483647},
    {"dynamic",0,1},{"damage_peak",1,2147483647},{"duration_min_ms",10,1000},{"duration_max_ms",10,1000}
};
static const int health_defaults[HR_COUNT]={0,0,2,0,0,0,100,180,120,200,0,1,0,120,60,500};
typedef struct {TitleRules parser;int config[HR_PROFILE_MAX][HR_COUNT];int count,rank,overflow;} HealthProfiles;
static inline int health_rule_header(char *line) {
    char *s=line;while(*s==' '||*s=='\t')s++;
    if(!strncmp(s,"\xef\xbb\xbf",3))s+=3;
    if(*s!='[')return 0;
    char *label=strchr(s,'|');if(!label)return 0;
    char *end=strchr(label,']');
    if(!end || end==label+1 || end-label>64)return -1;
    for(char *p=label+1;p<end;p++)if((unsigned char)*p<32 || *p=='[' || *p=='|')return -1;
    memmove(label,end,strlen(end)+1);return 0;
}
static inline void health_profiles_init(HealthProfiles *p) {
    memset(p,0,sizeof(*p));memcpy(p->parser.base,health_defaults,sizeof(health_defaults));
}
static inline void health_profiles_finish(HealthProfiles *p) {
    int rank=p->parser.rank;
    if(!rank || rank<p->rank)return;
    if(rank>p->rank){p->rank=rank;p->count=0;p->overflow=0;}
    if(p->count==HR_PROFILE_MAX){p->overflow=1;return;}
    memcpy(p->config[p->count++],p->parser.draft,sizeof(health_defaults));
}
static inline int health_profiles_line(HealthProfiles *p,char *line,const char *id,const char *path) {
    char *s=line;while(*s==' '||*s=='\t')s++;
    if(!strncmp(s,"\xef\xbb\xbf",3))s+=3;
    if(*s=='[')health_profiles_finish(p);
    if(health_rule_header(line)<0)return -1;
    return title_rules_line(&p->parser,line,id,path,health_keys,HR_COUNT);
}
static inline unsigned health_profiles_step(HealthProfiles *p,HealthRumble *states,uint64_t now,
                                            int active,HealthRead read,void *ctx) {
    unsigned strongest=0;
    for(int i=0;i<p->count;i++) {
        unsigned value=hr_step(states+i,p->config[i],now,active,read,ctx);
        if(value>strongest)strongest=value;
    }
    return strongest;
}
#endif

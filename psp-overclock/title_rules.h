/* SPDX-License-Identifier: MIT */
#ifndef PSP_TITLE_RULES_H
#define PSP_TITLE_RULES_H
#include <string.h>
#include <limits.h>
#define TITLE_RULE_MAX_KEYS 16
typedef struct { const char *name; int minimum, maximum; } TitleRuleKey;
typedef struct {
    int values[TITLE_RULE_MAX_KEYS], draft[TITLE_RULE_MAX_KEYS], base[TITLE_RULE_MAX_KEYS], rank, best, line, error;
    int section, selected_line;
} TitleRules;
static int title_rule_equal(const char *a,const char *b,int id)
{
    for(;;) {
        if(id){while(*a=='-')a++;while(*b=='-')b++;}
        unsigned char x=*a++,y=*b++;
        if(x>='a'&&x<='z')x-=32;
        if(y>='a'&&y<='z')y-=32;
        if(x!=y)return 0;
        if(!x)return 1;
    }
}
static void title_rules_finish(TitleRules *r)
{
    if(r->rank>r->best){memcpy(r->values,r->draft,sizeof(r->values));r->best=r->rank;r->selected_line=r->section;}
}
/* One bounded line at a time: no resident rule table or file-size allocation. */
static int title_rules_line(TitleRules *r,char *s,const char *id,const char *path,
                            const TitleRuleKey *keys,int count)
{
    char *end; int value=0,digits=0,k;
    if(count<1||count>TITLE_RULE_MAX_KEYS){r->error=r->line+1;return -1;}
    r->line++;
    if(r->line==1 && strlen(s)>=3 && (unsigned char)s[0]==0xef &&
       (unsigned char)s[1]==0xbb && (unsigned char)s[2]==0xbf)s+=3;
    while(*s==' '||*s=='\t')s++;
    if(!*s||*s=='#'||*s==';')return 0;
    end=s+strlen(s);
    while(end>s&&(end[-1]==' '||end[-1]=='\t'||end[-1]=='\r'))*--end=0;
    if(!*s)return 0;
    if(*s=='[') {
        title_rules_finish(r);
        if(end-s<3||end[-1]!=']')goto invalid;
        end[-1]=0;
        r->section=r->line;r->rank=0;
        memcpy(r->draft,r->base,sizeof(r->draft));
        if(!strncmp(s,"[title:",7)&&s[7])r->rank=(*id&&title_rule_equal(s+7,id,1))?2:0;
        else if(!strncmp(s,"[path:",6)&&s[6])r->rank=(*path&&title_rule_equal(s+6,path,0))?1:0;
        else goto invalid;
        return 0;
    }
    if(!r->section)goto invalid;
    end=strchr(s,'=');if(!end)goto invalid;
    char *v=end+1;
    while(end>s&&(end[-1]==' '||end[-1]=='\t'))end--;
    *end=0;
    for(k=0;k<count;k++)if(!strcmp(s,keys[k].name))break;
    if(k==count)goto invalid;
    while(*v==' '||*v=='\t')v++;
    int radix=10;
    if(v[0]=='0'&&(v[1]=='x'||v[1]=='X')){radix=16;v+=2;}
    for(;;){int digit=*v>='0'&&*v<='9'?*v-'0':radix==16&&*v>='a'&&*v<='f'?*v-'a'+10:radix==16&&*v>='A'&&*v<='F'?*v-'A'+10:-1;
        if(digit<0)break;
        if(value>(INT_MAX-digit)/radix)goto invalid;
        value=value*radix+digit;v++;digits++;}
    while(*v==' '||*v=='\t'||*v=='\r')v++;
    if(!digits||(*v&&*v!='#'&&*v!=';')||value<keys[k].minimum||value>keys[k].maximum)goto invalid;
    r->draft[k]=value;
    return 0;
invalid:
    r->error=r->line;return -1;
}
#endif

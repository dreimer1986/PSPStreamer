#include <assert.h>
#include "../psp-overclock/title_rules.h"
typedef struct { char title_id[16]; } SceGameInfo;
static SceGameInfo game={"ULUS12345"};
static const char *input;
static int position;
#define PSP_O_RDONLY 1
static SceGameInfo *sceKernelGetGameInfo(void){return &game;}
static int sceIoOpen(const char *p,int flags,int mode){(void)p;(void)flags;(void)mode;position=0;return input?1:(int)0x80010002;}
static int sceIoRead(int fd,void *out,int size){(void)fd;int n=strlen(input+position);if(n>size)n=size;memcpy(out,input+position,n);position+=n;return n;}
static int sceIoClose(int fd){(void)fd;return 0;}
#include "../psp-overclock/title_rules_io.h"
static const TitleRuleKey keys[]={{"enabled",0,1},{"target_mhz",66,471}};
static void line(TitleRules *r,const char *s)
{
    char text[384];strcpy(text,s);
    assert(!title_rules_line(r,text,"ULUS-12345","ms0:/PSP/GAME/Test/EBOOT.PBP",keys,2));
}
int main(void)
{
    TitleRules r={0};r.base[0]=1;r.base[1]=333;memcpy(r.values,r.base,sizeof(r.base));
    line(&r,"# comment");
    line(&r,"[path:MS0:/PSP/GAME/test/eboot.pbp]");line(&r,"target_mhz = 266");
    line(&r,"[title:ulus12345]");line(&r,"target_mhz=366 ; comment");
    line(&r,"[title:ULUS12345]");line(&r,"target_mhz=400");
    title_rules_finish(&r);assert(r.values[1]==366&&r.values[0]==1&&r.selected_line==4);
    line(&r,"[path:ms0:/PSP/GAME/]");line(&r,"enabled=0");
    title_rules_finish(&r);assert(r.values[0]==1);
    char invalid[]="target_mhz=9999999999999999999999999";
    assert(title_rules_line(&r,invalid,"","",keys,2)<0);
    assert(title_rule_equal("ULUS-12345","ulus12345",1));
    assert(!title_rule_equal("ULUS12345","ULUS12346",1));
    int values[]={1,333};
    assert(title_rules_load("file","",keys,2,values)==0&&values[1]==333);
    input="[title:ULUS12345]\nenabled=0\n[title:OTHER]\nbad=1\n";
    assert(title_rules_load("file","",keys,2,values)<0&&values[0]==1);
    input="\r\n[title:ULUS12345]\r\ntarget_mhz=266";
    assert(title_rules_load("file","",keys,2,values)==2&&values[1]==266);
    input="[title:OTHER]\ntarget_mhz=400\n";
    assert(title_rules_load("file","",keys,2,values)==0&&values[1]==266);
    TitleRuleKey address_key[]={ {"address",0,0x09ffffff} };int address[]={0};
    input="[title:ULUS12345]\naddress=0x08801000\n";
    assert(title_rules_load("file","",address_key,1,address)==1&&address[0]==0x08801000);
    input="[title:ULUS12345]\naddress=142610432\n";
    assert(title_rules_load("file","",address_key,1,address)==1&&address[0]==142610432);
    input="[title:ULUS12345]\naddress=0xFFFFFFFFFFFFFFFF\n";
    assert(title_rules_load("file","",address_key,1,address)<0);
    input="[title:ULUS12345]\naddress=2147483648\n";
    assert(title_rules_load("file","",address_key,1,address)<0);
    assert(title_rules_load("file","",keys,TITLE_RULE_MAX_KEYS+1,values)<0);
    return 0;
}

#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned long long tick;
static unsigned int clock_calls;
static unsigned long long sceKernelGetSystemTimeWide(void){clock_calls++;return tick;}
#include "milkdrop_profile.h"
int main(void) {
    char text[1024];
    md_profile_reset(0);md_profile_select("off",0,0,3);
    md_profile_begin();md_profile_mark(0);md_profile_commit();
    assert(!clock_calls && !md_profile_report(0,text,sizeof(text)));
    md_profile_reset(1);md_profile_select("test",0,0,3);
    for(int frame=1;frame<=2;frame++) {
        md_profile_begin();
        for(int i=0;i<MD_PROFILE_PHASES;i++){tick+=(i+1)*frame;md_profile_mark(i);}
        md_profile_commit();
    }
    assert(md_profile_report(0,text,sizeof(text)));
    assert(strstr(text,"frames=2") && strstr(text,"gpu_wait avg_us=10 max_us=14"));
    /* Abandoned/failed frame is never counted or mixed into the next one. */
    md_profile_begin();tick+=10000;md_profile_mark(0);
    md_profile_select("test",1,1,3);assert(md_profile_current==1);
    md_profile_begin();
    for(int i=0;i<MD_PROFILE_PHASES;i++){tick+=5;md_profile_mark(i);}
    md_profile_commit();
    assert(md_profile_report(1,text,sizeof(text)) && strstr(text,"TV full"));
    assert(strstr(text,"setup avg_us=5 max_us=5"));
    md_profile_select("test",0,0,3);assert(md_profile_current==0);
    for(int i=2;i<MD_PROFILE_RECORDS;i++){char name[20];snprintf(name,sizeof(name),"preset%d",i);md_profile_select(name,0,0,3);}
    md_profile_select("overflow",0,0,3);assert(md_profile_current==-1);
    unsigned int before=clock_calls;md_profile_begin();md_profile_mark(0);md_profile_commit();
    assert(clock_calls==before && md_profile_dropped==1);
    md_profile_select("test",0,0,3);assert(md_profile_current==0);
    assert(md_profile_report(0,text,1) && text[0]==0);
    assert(!md_profile_report(-1,text,sizeof(text)));
    md_profile_reset(0);assert(!md_profile_report(0,text,sizeof(text)));
    return 0;
}

/* Bounded text page parser; usable by host tests without PSP headers. */
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#define SUBTITLE_PAGE_CUES 256
typedef struct {
    int count, offset, next, until;
    SubtitleCue cues[SUBTITLE_PAGE_CUES];
} SubtitlePage;
static int subtitle_page_integer(const char *json,const char *key,int *value) {
    const char *p=strstr(json,key);char *end;long n;
    if(!p)return 0;
    p+=strlen(key);errno=0;n=strtol(p,&end,10);
    if(end==p || errno || n < -1 || n>INT_MAX || (*end!=',' && *end!='}'))return 0;
    *value=(int)n;return 1;
}
static int subtitle_page_parse(const char *json,SubtitlePage *page) {
    int paged=0;
    const char *p=strstr(json,"\"c\":[");
    page->count=0;
    if(!p || !strstr(json,"\"t\":\"text\"") ||
       !subtitle_page_integer(json,"\"paged\":",&paged) || paged!=1 ||
       !subtitle_page_integer(json,"\"offset\":",&page->offset) || page->offset<0 ||
       !subtitle_page_integer(json,"\"next\":",&page->next) ||
       !subtitle_page_integer(json,"\"until\":",&page->until) || page->until<0)return 0;
    p+=5;
    for(;;) {
        while(isspace((unsigned char)*p))p++;
        if(*p==']')break;
        if(page->count>=SUBTITLE_PAGE_CUES)return 0;
        SubtitleCue *cue=page->cues+page->count;char *end;long start,finish;
        if(*p++!='[')return 0;
        errno=0;start=strtol(p,&end,10);
        if(errno || end==p || *end!=',' || start<0 || start>INT_MAX)return 0;
        p=end+1;errno=0;finish=strtol(p,&end,10);
        if(errno || end==p || *end!=',' || finish<0 || finish>INT_MAX)return 0;
        p=end+1;if(*p++!='"')return 0;
        const char *close=strchr(p,'"');
        if(!close || close==p || close-p>159 || close[1]!=']')return 0;
        for(const char *s=p;s<close;s++)if(*s=='\\' || (unsigned char)*s<32)return 0;
        memcpy(cue->text,p,(size_t)(close-p));cue->text[close-p]=0;
        cue->start_ms=(int)start;cue->end_ms=(int)finish;
        if(cue->end_ms<=cue->start_ms || cue->end_ms>page->until ||
           (page->count && cue->start_ms<page->cues[page->count-1].end_ms))return 0;
        page->count++;p=close+2;
        if(*p==','){p++;if(*p!='[')return 0;}
        else if(*p!=']')return 0;
    }
    if(p[1]!='}' || p[2])return 0;
    return page->next==-1 || (page->count && page->offset<=INT_MAX-page->count &&
                             page->next==page->offset+page->count);
}

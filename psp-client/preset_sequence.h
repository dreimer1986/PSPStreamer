#ifndef PSPSTREAMER_PRESET_SEQUENCE_H
#define PSPSTREAMER_PRESET_SEQUENCE_H
#include "preset_catalog.h"
#include <math.h>
typedef struct { PresetCatalog catalog; float rating[PRESET_CATALOG_LIMIT]; unsigned int random; } PresetSequence;
static float preset_rating(const char *directory,const char *name) {
    char path[512],line[256]; float result=3;
    if(snprintf(path,sizeof(path),"%s/%s",directory,name)>=(int)sizeof(path))return -1;
    FILE *f=fopen(path,"rb");if(!f) return -1;
    int bytes=0;
    while(fgets(line,sizeof(line),f) && (bytes+=(int)strlen(line))<=16384) {
        char *p=line;while(*p==' ' || *p=='\t') p++;
        if(!strncmp(p,"fRating",7)) {
            p+=7;while(*p==' ' || *p=='\t') p++;
            if(*p=='=') {char *end;float n=strtof(p+1,&end);if(end!=p+1 && isfinite(n) && n>=0 && n<=5) result=n;}
        }
    }
    fclose(f);return result;
}
static int preset_sequence_load(PresetSequence *s,const char *directory,unsigned int seed) {
    s->random=seed?seed:1;
    if(!preset_catalog_load(&s->catalog,directory)) return 0;
    char path[512],line[512];
    if(snprintf(path,sizeof(path),"%s/playlist.txt",directory)>=(int)sizeof(path)){s->catalog.count=0;return 0;}
    FILE *f=fopen(path,"rb");
    if(f) {
        PresetCatalog *selected=calloc(1,sizeof(*selected));
        if(!selected) {fclose(f);s->catalog.count=0;return 0;}
        while(fgets(line,sizeof(line),f)) {
            size_t n=strlen(line);while(n && (line[n-1]=='\n' || line[n-1]=='\r')) line[--n]=0;
            if(!n || line[0]=='#') continue;
            if(!preset_name_valid(line)) continue;
            for(int i=0;i<s->catalog.count;i++) if(!strcmp(line,s->catalog.names[i])) {
                if(selected->count<PRESET_CATALOG_LIMIT) strcpy(selected->names[selected->count++],line);
                else selected->truncated=1;
                break;
            }
        }
        fclose(f);s->catalog=*selected;free(selected);
    }
    for(int i=0;i<s->catalog.count;i++) s->rating[i]=preset_rating(directory,s->catalog.names[i]);
    return s->catalog.count;
}
/* mode 1 ordered; 2 uniform random; 3 fRating-weighted random. No immediate repeats. */
static inline int preset_sequence_load_selected(PresetSequence *s,const char *root,const char *current,unsigned int seed) {
    char directory[512],prefix[256]="";
    const char *slash=strrchr(current,'/');
    if(slash && preset_path_valid(current)){size_t n=slash-current+1;memcpy(prefix,current,n);prefix[n]=0;}
    if(snprintf(directory,sizeof(directory),"%s/%s",root,prefix)>=(int)sizeof(directory)){s->catalog.count=0;return 0;}
    int count=preset_sequence_load(s,directory,seed);
    for(int i=0;i<count;i++) {
        char relative[512];snprintf(relative,sizeof(relative),"%s%s",prefix,s->catalog.names[i]);
        if(!preset_path_valid(relative)){s->rating[i]=-1;continue;}
        strcpy(s->catalog.names[i],relative);
    }
    return count;
}
static int preset_sequence_next(PresetSequence *s,const char *current,int mode) {
    int current_index=-1;
    for(int i=0;i<s->catalog.count;i++) if(!strcmp(current,s->catalog.names[i])) {current_index=i;break;}
    if(mode==1) {
        for(int step=1;step<=s->catalog.count;step++) {
            int i=(current_index+step)%s->catalog.count;
            if(s->rating[i]>=0 && strcmp(current,s->catalog.names[i])) return i;
        }
        return -1;
    }
    if(mode!=2 && mode!=3) return -1;
    float total=0;
    for(int i=0;i<s->catalog.count;i++) if(s->rating[i]>=0 && strcmp(current,s->catalog.names[i])) total+=mode==3?s->rating[i]:1;
    if(total<=0) return -1;
    s->random=s->random*1664525U+1013904223U;
    float choose=(s->random>>8)*(1.0f/16777216.0f)*total;
    for(int i=0;i<s->catalog.count;i++) if(s->rating[i]>=0 && strcmp(current,s->catalog.names[i])) {
        choose-=mode==3?s->rating[i]:1;if(choose<0) return i;
    }
    return -1;
}
#endif

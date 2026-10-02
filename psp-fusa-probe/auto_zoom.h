/* SPDX-License-Identifier: MIT */
#ifndef FS_AUTO_ZOOM_H
#define FS_AUTO_ZOOM_H
#include <string.h>
typedef struct { unsigned long long since; int timing,handled; } FsAutoZoom;
/* Read one bounded INI line. Unknown/malformed entries preserve defaults. */
static void fs_auto_option_ex(char *line,int *enabled,unsigned *seconds,int *keep,int *speedboost)
{
    char *p=line;while(*p==' '||*p=='\t')p++;
    char *key=p;while(*p&&*p!='='&&*p!=' '&&*p!='\t')p++;
    char *end=p;while(*p==' '||*p=='\t')p++;
    if(*p!='=')return;
    *end=0;p++;while(*p==' '||*p=='\t')p++;
    if(*p<'0'||*p>'9')return;
    unsigned value=0;
    while(*p>='0'&&*p<='9'){value=value*10+(*p++-'0');if(value>60)return;}
    while(*p==' '||*p=='\t'||*p=='\r')p++;
    if(*p&&*p!=';'&&*p!='#')return;
    if(!strcmp(key,"auto_zoom")&&value<=1)*enabled=value;
    if(!strcmp(key,"auto_zoom_delay_seconds")&&value>=1)*seconds=value;
    if(!strcmp(key,"keep_fullscreen")&&value<=1)*keep=value;
    if(speedboost&&!strcmp(key,"experimental_speedboost")&&value<=1)*speedboost=value;
}
static inline void fs_auto_option(char *line,int *enabled,unsigned *seconds,int *keep)
{fs_auto_option_ex(line,enabled,seconds,keep,NULL);}
/* User intent survives automatic restoration, but not an explicit toggle off. */
static int fs_zoom_wanted(int automatic,int keep,int armed)
{return automatic||(keep&&armed);}
static int fs_button_exit(int keep,unsigned buttons,unsigned mask)
{return !keep&&(buttons&mask)!=0;}
static int fs_auto_tick(FsAutoZoom *state,unsigned long long now,int tv,unsigned delay)
{
    if(!tv){state->timing=0;state->handled=0;return 0;}
    if(state->handled)return 0;
    if(!state->timing){state->timing=1;state->since=now;return 0;}
    if(now-state->since<(unsigned long long)delay*1000000ULL)return 0;
    state->handled=1;return 1;
}
#endif

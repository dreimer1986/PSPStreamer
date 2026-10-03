/* Bounded, lossless INI edits. No resident allocation outside settings. */
#ifndef PLUGIN_SETTINGS_IO_H
#define PLUGIN_SETTINGS_IO_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "../psp-overclock/title_rules.h"
#define PI_CAP 65537
typedef struct { char *text; size_t length,limit; char path[192]; } PluginIni;
static int pi_open(PluginIni *d,const char *path,size_t limit) {
    memset(d,0,sizeof(*d));d->limit=limit;
    if(strlen(path)>=sizeof(d->path))return -1;
    strcpy(d->path,path);d->text=calloc(1,PI_CAP);if(!d->text)return -1;
    FILE *f=fopen(path,"rb");
    if(!f){if(errno==ENOENT)return 0;free(d->text);d->text=NULL;return -1;}
    d->length=fread(d->text,1,PI_CAP-1,f);int bad=ferror(f)||fgetc(f)!=EOF;
    if(fclose(f))bad=1;
    if(bad||d->length>limit||memchr(d->text,0,d->length)){free(d->text);d->text=NULL;return -1;}
    return 0;
}
static size_t pi_next(const PluginIni *d,size_t p) {
    while(p<d->length&&d->text[p]!='\n')p++;
    return p<d->length?p+1:p;
}
static int pi_line(const PluginIni *d,size_t p,char *s,size_t cap) {
    size_t e=pi_next(d,p),n=e-p;
    while(n&&(d->text[p+n-1]=='\r'||d->text[p+n-1]=='\n'))n--;
    if(n>=cap)return -1;
    memcpy(s,d->text+p,n);s[n]=0;
    if(p==0&&n>=3&&!memcmp(s,"\xef\xbb\xbf",3))memmove(s,s+3,n-2);
    return 0;
}
static int pi_replace(PluginIni *d,size_t a,size_t b,const char *s) {
    size_t n=strlen(s);if(a>b||b>d->length||d->length-(b-a)+n>d->limit)return -1;
    memmove(d->text+a+n,d->text+b,d->length-b+1);memcpy(d->text+a,s,n);
    d->length=d->length-(b-a)+n;return 0;
}
static char *pi_trim(char *s) {while(*s==' '||*s=='\t')s++;return s;}
static int pi_section(const PluginIni *d,int index,size_t *a,size_t *b,char *name) {
    int at=-1;*a=*b=d->length;
    for(size_t p=0;p<d->length;p=pi_next(d,p)) {
        char s[384];if(pi_line(d,p,s,sizeof(s)))return -1;
        char *v=pi_trim(s);if(*v!='[')continue;
        at++;if(at==index){*a=p;if(name)snprintf(name,384,"%s",v);}
        else if(at==index+1){*b=p;return 0;}
    }
    return at>=index?0:-1;
}
static int pi_key(const char *line,const char *key,const char **value) {
    while(*line==' '||*line=='\t')line++;
    if(!strncmp(line,"\xef\xbb\xbf",3))line+=3;
    size_t n=strlen(key);if(strncmp(line,key,n))return 0;line+=n;
    while(*line==' '||*line=='\t')line++;
    if(*line++!='=')return 0;
    while(*line==' '||*line=='\t')line++;
    *value=line;return 1;
}
static int pi_get(const PluginIni *d,int section,const char *key,int fallback) {
    size_t a=0,b=d->length;if(section>=0&&pi_section(d,section,&a,&b,NULL))return fallback;
    for(size_t p=a;p<b;p=pi_next(d,p)) {
        char s[384];const char *v;if(!pi_line(d,p,s,sizeof(s))&&pi_key(s,key,&v)){
            char *end;errno=0;long n=strtol(v,&end,v[0]=='0'&&(v[1]=='x'||v[1]=='X')?16:10);
            if(!errno&&end!=v&&n>=0&&n<=INT_MAX)fallback=(int)n;
        }
    }
    return fallback;
}
/* Remove every duplicate of this key in the selected scope, preserve all
 * other keys/comments/sections, then append the replacement to that scope. */
static int pi_set(PluginIni *d,int section,const char *key,int value) {
    char *saved=malloc(d->length+1);if(!saved)return -1;
    size_t original=d->length;memcpy(saved,d->text,original+1);
    size_t a=0,b=d->length;if(section>=0&&pi_section(d,section,&a,&b,NULL)){free(saved);return -1;}
    for(size_t p=a;p<b;) {
        char s[384];const char *v;size_t next=pi_next(d,p);
        if(!pi_line(d,p,s,sizeof(s))&&pi_key(s,key,&v)) {pi_replace(d,p,next,"");b-=next-p;}else p=next;
    }
    int rc=0;if(value>=0){char row[128];snprintf(row,sizeof(row),"%s%s=%d\n",b&&d->text[b-1]!='\n'?"\n":"",key,value);rc=pi_replace(d,b,b,row);}
    if(rc){memcpy(d->text,saved,original+1);d->length=original;}free(saved);return rc;
}
static int pi_validate_rules(const PluginIni *d,const TitleRuleKey *keys,int count) {
    TitleRules r={0};for(size_t p=0;p<d->length;p=pi_next(d,p)) {
        char s[384];if(pi_line(d,p,s,sizeof(s))||title_rules_line(&r,s,"","",keys,count))return -1;
    }return 0;
}
static int pi_save(PluginIni *d) {
    char tmp[208],bak[208];snprintf(tmp,sizeof(tmp),"%s.tmp",d->path);snprintf(bak,sizeof(bak),"%s.bak",d->path);
    FILE *f=fopen(tmp,"wb");if(!f)return -1;
    int bad=fwrite(d->text,1,d->length,f)!=d->length;
    if(fflush(f))bad=1;
    if(fclose(f))bad=1;
    if(bad)return -1;
    /* Verify the complete temporary file before touching the existing INI. */
    f=fopen(tmp,"rb");if(!f)return -1;
    size_t i=0;int c;while((c=fgetc(f))!=EOF){if(i>=d->length||(unsigned char)c!=(unsigned char)d->text[i++])bad=1;}
    if(i!=d->length||ferror(f))bad=1;
    if(fclose(f))bad=1;
    if(bad)return -1;
    f=fopen(d->path,"rb");int exists=f!=NULL;if(f)fclose(f);else if(errno!=ENOENT)return -1;
    if(exists){if(remove(bak)&&errno!=ENOENT)return -1;if(rename(d->path,bak))return -1;}
    if(rename(tmp,d->path)){if(exists)rename(bak,d->path);return -1;}return 0;
}
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
typedef int SceUID;
typedef struct {unsigned int size;char s_name[13],padding[3],l_name[1024];} SceIoFatDirentPrivate;
typedef struct {struct {int st_mode;long long st_size;} d_stat;char d_name[256];SceIoFatDirentPrivate *d_private;} SceIoDirent;
#define PSP_O_RDONLY 0
#define FIO_S_ISDIR(mode) ((mode)==1)
static int direct,legacy,entries=1,mismatch,directory,bad_alias,read_error,opens,closed;
static int sceIoOpen(const char *path,int flags,int mode) {
    (void)flags;(void)mode;opens++;
    if(direct)return direct;
    return !strcmp(path,"ms0:/PSP/VIDEO/PSPStreamer/job/ANARCH~1.FLV")?5:(int)0x80010002;
}
static int sceIoDopen(const char *path) {assert(!strcmp(path,"ms0:/PSP/VIDEO/PSPStreamer/job"));return 2;}
static int sceIoDread(int fd,SceIoDirent *entry) {
    assert(fd==2 && entry->d_private && entry->d_private->size==1044);
    if(read_error)return -1;
    if(!entries)return 0;
    entries--;
    entry->d_stat.st_size=mismatch?99:123456789;
    entry->d_stat.st_mode=directory;
    strcpy(legacy?(char *)entry->d_private:entry->d_private->s_name,bad_alias?"../BAD.FLV":"ANARCH~1.FLV");
    return 1;
}
static void sceIoDclose(int fd) {assert(fd==2);closed++;}
#include "offline_filename.h"
static int attempt(unsigned long long size) {
    char path[512]="ms0:/PSP/VIDEO/PSPStreamer/job/Episode - Grüße.flv";
    int result=offline_open_movie(path,sizeof(path),size);
    if(result==5)assert(!strcmp(path,"ms0:/PSP/VIDEO/PSPStreamer/job/ANARCH~1.FLV"));
    else assert(strstr(path,"Grüße"));
    return result;
}
int main(void) {
    assert(offline_manifest_movie_size("{\"files\":[{\"name\":\"Grüße.flv\",\"size\":123456789}]}")==123456789);
    assert(!offline_manifest_movie_size("{}"));
    assert(!offline_manifest_movie_size("{\"files\":[{}],\"size\":4}"));
    direct=7;assert(attempt(123456789)==7 && !closed);
    direct=-3;assert(attempt(123456789)==-3 && !closed);
    direct=0;assert(attempt(0)==(int)0x80010002 && !closed);
    for(legacy=0;legacy<=1;legacy++){entries=1;assert(attempt(123456789)==5);}
    legacy=0;
    entries=2;assert(attempt(123456789)==(int)0x80010002);
    entries=1;mismatch=1;assert(attempt(123456789)==(int)0x80010002);mismatch=0;
    entries=1;directory=1;assert(attempt(123456789)==(int)0x80010002);directory=0;
    entries=1;bad_alias=1;assert(attempt(123456789)==(int)0x80010002);bad_alias=0;
    entries=1;read_error=1;assert(attempt(123456789)==(int)0x80010002);
    assert(!offline_short_flv("PART.FLV.PART",13));
    assert(!offline_short_flv("123456789.FLVX",13));
    assert(!offline_short_flv("EÄ.FLV",13));
    return 0;
}

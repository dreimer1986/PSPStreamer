#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned long long u64;
typedef int SceUID;
typedef struct {int year;u64 stamp;} ScePspDateTime;
typedef struct {int st_mode;ScePspDateTime sce_st_mtime;} SceIoStat;
typedef struct {SceIoStat d_stat;char d_name[256];} SceIoDirent;
#define FIO_S_ISREG(mode) ((mode)==1)
static struct {char name[192];SceIoStat stat;} files[32];
static int count,index_dir,removed,renamed,bad_clock,rename_fail;
static u64 now=200000000000ULL;
static int sceRtcGetCurrentClockLocalTime(ScePspDateTime *d){d->year=bad_clock?2000:2026;d->stamp=now;return 0;}
static int sceRtcGetTick(const ScePspDateTime *d,u64 *t){*t=d->stamp;return 0;}
static u64 sceKernelGetSystemTimeWide(void){return 123;}
static int lookup(const char *p){
    const char *name=strrchr(p,'/');name=name?name+1:p;
    for(int i=0;i<count;i++)if(!strcmp(files[i].name,name))return i;
    return -1;
}
static int sceIoDopen(const char *p){(void)p;index_dir=0;return 1;}
static int sceIoDread(int fd,SceIoDirent *e){
    (void)fd;if(index_dir>=count)return 0;
    strcpy(e->d_name,files[index_dir].name);e->d_stat=files[index_dir++].stat;return 1;
}
static int sceIoDclose(int fd){(void)fd;return 0;}
static int sceIoGetstat(const char *p,SceIoStat *s){int i=lookup(p);if(i<0)return -1;*s=files[i].stat;return 0;}
static int sceIoRemove(const char *p){int i=lookup(p);assert(i>=0);files[i].name[0]=0;removed++;return 0;}
static int sceIoRename(const char *p,const char *q){
    if(rename_fail)return -1;
    int i=lookup(p);assert(i>=0 && lookup(q)<0);
    strcpy(files[i].name,strrchr(q,'/')+1);renamed++;return 0;
}
static void add(const char *name,u64 age,int regular){
    strcpy(files[count].name,name);files[count].stat.st_mode=regular;
    files[count].stat.sce_st_mtime.stamp=now-age;count++;
}
#include "diagnostic_history.h"
int main(void){
    assert(diagnostic_name_owned("PSPStreamer-watch-video.txt"));
    assert(diagnostic_name_owned("PSPStreamer-watch-video.txt.history-00000000000000AF"));
    assert(!diagnostic_name_owned("PSPStreamer.cfg"));
    assert(!diagnostic_name_owned("PSPStreamer-watch-video.txt.history-user-notes"));
    add("PSPStreamer.cfg",100000000000ULL,1);
    add("PSPStreamer-watch-video.txt",100000000000ULL,1);
    add("PSPStreamer-watch-music.txt",1,1);
    add("PSPStreamer-sync-tv.csv.history-00000000000000AF",100000000000ULL,1);
    add("PSPStreamer-sync-lcd.csv",100000000000ULL,0); /* Never remove a directory. */
    bad_clock=1;diagnostic_prune();assert(!removed);
    bad_clock=0;diagnostic_prune();assert(removed==2 && lookup("PSPStreamer.cfg")>=0);
    assert(diagnostic_rotate("ms0:/PSP/SYSTEM/PSPStreamer-watch-music.txt")==0 && renamed==1);
    add("PSPStreamer-watch-music.txt",1,1);
    assert(diagnostic_rotate("ms0:/PSP/SYSTEM/PSPStreamer-watch-music.txt")==0 && renamed==2);
    add("PSPStreamer-watch-music.txt",1,1);rename_fail=1;
    assert(diagnostic_rotate("ms0:/PSP/SYSTEM/PSPStreamer-watch-music.txt")<0);
    assert(lookup("PSPStreamer-watch-music.txt")>=0);
    rename_fail=0;diagnostic_history_start();
    assert(lookup("PSPStreamer-watch-music.txt")<0 && lookup("PSPStreamer.cfg")>=0);
    puts("diagnostic retention, restart rotation, name whitelist, clock safety, collisions and failure preservation: OK");
}

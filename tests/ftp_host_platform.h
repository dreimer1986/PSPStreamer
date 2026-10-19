/* Host shims for the ACTUAL PSP FTP state machine, rooted in a test directory. */
#ifndef FTP_HOST_PLATFORM_H
#define FTP_HOST_PLATFORM_H
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include "../streammaster/protocol.h"
#include "../streammaster/tcp_server.h"
#define PSP_MODULE_INFO(...)
#define PSP_MODULE_KERNEL 0
#define PSP_INIT_KEYCONFIG_VSH 0x100
#define PSP_SMEM_High 1
#define PSP_O_RDONLY O_RDONLY
#define PSP_O_WRONLY O_WRONLY
#define PSP_O_CREAT O_CREAT
#define PSP_O_TRUNC O_TRUNC
#define PSP_O_EXCL O_EXCL
#define PSP_SEEK_SET SEEK_SET
#define FIO_S_ISDIR S_ISDIR
typedef int SceUID;
typedef unsigned SceSize;
typedef unsigned SceUInt;
typedef int64_t SceOff;
typedef struct {unsigned year,month,day,hour,minute,second;} ScePspDateTime;
typedef struct {unsigned st_mode;SceOff st_size;ScePspDateTime sce_st_mtime;} SceIoStat;
typedef struct {SceIoStat d_stat;char d_name[256];} SceIoDirent;
static DIR *host_dirs[16];
static char host_dirnames[16][1024];
static void *host_block;
static const char *host_path(const char *p){if(strncmp(p,"ms0:/",5))abort();return p[5]?p+5:".";}
static int sceIoOpen(const char *p,int flags,int mode){return open(host_path(p),flags,mode);}
static int sceIoClose(int f){return close(f);}
static int sceIoRead(int f,void *p,unsigned n){return read(f,p,n);}
static int sceIoWrite(int f,const void *p,unsigned n){return write(f,p,n);}
static SceOff sceIoLseek(int f,SceOff offset,int how){return lseek(f,offset,how);}
static int sceIoRename(const char *a,const char *b){return rename(host_path(a),host_path(b));}
static int sceIoRemove(const char *p){return unlink(host_path(p));}
static int sceIoMkdir(const char *p,int mode){return mkdir(host_path(p),mode);}
static int sceIoRmdir(const char *p){return rmdir(host_path(p));}
static void host_stat(const struct stat *st,SceIoStat *out){
    struct tm t;gmtime_r(&st->st_mtime,&t);
    *out=(SceIoStat){st->st_mode,st->st_size,{t.tm_year+1900,t.tm_mon+1,t.tm_mday,t.tm_hour,t.tm_min,t.tm_sec}};
}
static int sceIoGetstat(const char *p,SceIoStat *out){
    /* PSP device roots may be listable without normal Getstat metadata. */
    if(!strcmp(p,"ms0:/"))return -1;
    struct stat st;if(stat(host_path(p),&st)<0)return errno==ENOENT?(int)0x80010002:-1;
    host_stat(&st,out);return 0;
}
static int sceIoDopen(const char *p){
    for(unsigned i=0;i<16;i++)if(!host_dirs[i]){
        DIR *d=opendir(host_path(p));if(!d)return -1;host_dirs[i]=d;snprintf(host_dirnames[i],1024,"%s",host_path(p));return i;
    }return -1;
}
static int sceIoDclose(int d){int rc=closedir(host_dirs[d]);host_dirs[d]=NULL;return rc;}
static int sceIoDread(int d,SceIoDirent *e){
    struct dirent *entry=readdir(host_dirs[d]);if(!entry)return 0;
    snprintf(e->d_name,sizeof(e->d_name),"%s",entry->d_name);
    char path[1300];snprintf(path,sizeof(path),"%s/%s",host_dirnames[d],entry->d_name);
    struct stat st;if(stat(path,&st)<0)return -1;host_stat(&st,&e->d_stat);return 1;
}
static uint64_t sceKernelGetSystemTimeWide(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
static void sceKernelDelayThread(unsigned us){usleep(us==100000?1000:us);}
static int sceKernelInitKeyConfig(void){return PSP_INIT_KEYCONFIG_VSH;}
static int sceKernelGetSystemStatus(void){return 0x20000;}
static void *sceKernelFindModuleByName(const char *name){(void)name;return (void *)1;}
static int sceKernelAllocPartitionMemory(int p,const char *n,int k,unsigned size,void *at){(void)p;(void)n;(void)k;(void)at;host_block=calloc(1,size);return host_block?1:-1;}
static void *sceKernelGetBlockHeadAddr(int id){(void)id;return host_block;}
static void sceKernelFreePartitionMemory(int id){(void)id;free(host_block);host_block=NULL;}
static int sceKernelCreateThread(const char *n,int (*f)(SceSize,void *),int p,int st,int attr,void *opt){(void)n;(void)f;(void)p;(void)st;(void)attr;(void)opt;return -1;}
static int sceKernelStartThread(int id,unsigned n,void *p){(void)id;(void)n;(void)p;return -1;}
static int sceKernelDeleteThread(int id){(void)id;return 0;}
static int sceKernelWaitThreadEnd(int id,unsigned *t){(void)id;(void)t;return 0;}
void sm_tcp_server_command(const SmFrame *,SmFrame *);
void sm_tcp_server_idle(void);
static int sceIoDevctl(const char *name,unsigned op,void *in,unsigned n,void *out,unsigned cap){
    (void)name;
    if(op==SM_DEV_RESIDENT)return SM_RESIDENT_MAGIC;
    if(op==SM_DEV_STATUS)return 1;
    if(op!=SM_DEV_EXCHANGE_COMPACT || n!=sizeof(SmFrame) || cap!=sizeof(SmFrame))abort();
    SmFrame *r=in,*reply=out;if(!sm_valid(r))abort();
    memset(reply,0,sizeof(*reply));reply->op=r->op;reply->sequence=r->sequence;reply->flags=SM_REPLY;
    sm_tcp_server_idle();
    if(r->op==SM_CAPABILITIES){uint32_t caps=SM_CAP_TCP_SERVER;memcpy(reply->payload,&caps,4);reply->length=4;}
    else if(r->op==SM_INFO){SmInfo info={.wifi_state=SM_WIFI_READY};strcpy(info.ip,"127.0.0.1");memcpy(reply->payload,&info,sizeof(info));reply->length=sizeof(info);}
    else sm_tcp_server_command(r,reply);
    sm_seal(reply);return 0;
}
#endif

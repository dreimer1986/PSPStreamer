/* Bounded, versioned local state. Never written by decoder/audio workers. */
#ifndef COMFORT_STORE_H
#define COMFORT_STORE_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define COMFORT_RECORDS 64
#define COMFORT_PROFILES 5
typedef struct {
    char scope[80], id[512], name[128];
    int folder, audio, favorite, seconds;
    uint32_t used;
} ComfortRecord;
typedef struct {char name[32],host[64],password[129];int port,https;} ComfortProfile;
typedef struct {
    uint32_t magic,version,sequence;
    ComfortRecord records[COMFORT_RECORDS];
    ComfortProfile profiles[COMFORT_PROFILES];
    uint32_t checksum;
} ComfortStore;
static int comfort_recovered_backup;
static uint32_t comfort_checksum(const ComfortStore *s) {
    const unsigned char *p=(const unsigned char *)s;
    uint32_t hash=2166136261u;
    for(size_t i=0;i<offsetof(ComfortStore,checksum);i++)hash=(hash^p[i])*16777619u;
    return hash;
}
static int comfort_read_file(const char *path,ComfortStore *s) {
    FILE *f=fopen(path,"rb");if(!f)return 0;
    int ok=fread(s,1,sizeof(*s),f)==sizeof(*s) && fgetc(f)==EOF;fclose(f);
    if(!ok||s->magic!=0x50534346||s->version!=1||s->checksum!=comfort_checksum(s))return 0;
    for(int i=0;i<COMFORT_RECORDS;i++) {
        ComfortRecord *r=&s->records[i];
        if(!memchr(r->scope,0,sizeof(r->scope))||!memchr(r->id,0,sizeof(r->id))||!memchr(r->name,0,sizeof(r->name))||
           r->seconds<0||r->seconds>86400||r->folder<0||r->folder>1||r->audio<0||r->audio>1)return 0;
    }
    for(int i=0;i<COMFORT_PROFILES;i++) {
        ComfortProfile *p=&s->profiles[i];
        if(!memchr(p->name,0,sizeof(p->name))||!memchr(p->host,0,sizeof(p->host))||!memchr(p->password,0,sizeof(p->password))||
           (p->host[0]&&(p->port<1||p->port>65535||p->https<0||p->https>1)))return 0;
    }
    return 1;
}
static void comfort_load_file(const char *path,ComfortStore *s) {
    char backup[256];snprintf(backup,sizeof(backup),"%s.bak",path);
    comfort_recovered_backup=0;
    if(comfort_read_file(path,s))return;
    if(comfort_read_file(backup,s)){comfort_recovered_backup=1;return;}
    memset(s,0,sizeof(*s));s->magic=0x50534346;s->version=1;
}
static int comfort_save_file(const char *path,ComfortStore *s) {
    char temp[256],backup[256];snprintf(temp,sizeof(temp),"%s.tmp",path);snprintf(backup,sizeof(backup),"%s.bak",path);
    s->checksum=comfort_checksum(s);
    FILE *f=fopen(temp,"wb");if(!f)return 0;
    int ok=fwrite(s,1,sizeof(*s),f)==sizeof(*s);if(fclose(f))ok=0;
    if(!ok){remove(temp);return 0;}
    /* Keep the previous complete state if power is lost during replacement. */
    FILE *old=fopen(path,"rb");
    if(old){
        fclose(old);
        if(comfort_recovered_backup) {if(remove(path)){remove(temp);return 0;}}
        else {remove(backup);if(rename(path,backup)){remove(temp);return 0;}}
    }
    if(rename(temp,path)){rename(backup,path);return 0;}
    comfort_recovered_backup=0;
    return 1;
}
static int comfort_find(ComfortStore *s,const char *scope,const char *id,int create) {
    int candidate=-1;
    for(int i=0;i<COMFORT_RECORDS;i++) {
        ComfortRecord *r=&s->records[i];
        if(r->id[0]&&!strcmp(r->scope,scope)&&!strcmp(r->id,id))return i;
        if(!r->favorite && (candidate<0||r->used<s->records[candidate].used))candidate=i;
    }
    if(!create||candidate<0)return -1;
    ComfortRecord *r=&s->records[candidate];memset(r,0,sizeof(*r));
    snprintf(r->scope,sizeof(r->scope),"%s",scope);snprintf(r->id,sizeof(r->id),"%s",id);return candidate;
}
#endif

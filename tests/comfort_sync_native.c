#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "comfort_store.h"
static ComfortStore comfort_store;
static int network_ready=1,http_ready=1,audio_running,timed_running;
#define COMFORT_PATH "ms0:/PSP/SYSTEM/PSPStreamer.state"
#define TXT_COMFORT 0
#define TXT_PLEASE_WAIT 0
#define TXT_COMFORT_HELP 0
static const char *tr(int id){(void)id;return "test";}
static void settings_shell(const char *s){(void)s;}
static void settings_line(int r,int selected,const char *s){(void)r;(void)selected;(void)s;}
static void settings_help(const char *s){(void)s;}
static void comfort_scope(char *out,int local){(void)local;strcpy(out,"server:8091:0");}
static unsigned long long sceKernelGetSystemTimeWide(void){return 12345;}
static int input_hex(int c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
static int calls;
static int remote_http_request_budget(const char *path,char *reply,int capacity,volatile int *run,int ms,const char *body){
    assert(!strcmp(path,"/api/comfort/sync")&&*run&&ms==10000);
    assert(!strstr(body,"secret-password")&&!strstr(body,"local-id"));
    assert(strstr(body,"6d65646961"));calls++;
    return snprintf(reply,capacity,"{\"records\":[[\"6d65646961\",\"4772c3bcc39f65\",0,0,1,42,10],[\"6e6577\",\"4e6577\",0,1,1,0,11]]}");
}
#include "comfort_sync.h"
int main(void){
    comfort_load_file(COMFORT_PATH,&comfort_store);
    strcpy(comfort_store.profiles[0].password,"secret-password");
    int i=comfort_find(&comfort_store,"server:8091:0","media",1);
    strcpy(comfort_store.records[i].name,"Title");
    i=comfort_find(&comfort_store,"local","local-id",1);comfort_store.records[i].favorite=1;
    strcpy(comfort_store.records[i].name,"Local");
    assert(comfort_sync());
    i=comfort_find(&comfort_store,"server:8091:0","media",0);
    assert(i>=0&&comfort_store.records[i].favorite&&comfort_store.records[i].seconds==42);
    assert(!strcmp(comfort_store.records[i].name,"Grüße"));
    assert(comfort_find(&comfort_store,"server:8091:0","new",0)>=0);
    assert(comfort_find(&comfort_store,"local","local-id",0)>=0);
    assert(!strcmp(comfort_store.profiles[0].password,"secret-password"));
    audio_running=1;assert(!comfort_sync()&&calls==1);
    return 0;
}

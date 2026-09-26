#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
static int debug_enabled=1,logs,adds,add_result=7,in_callback;
static char last_stage[180];
static int last_error,last_event;
static unsigned long long sceKernelGetSystemTimeWide(void){return 123456000ULL;}
static int sceNetApctlAddHandler(void (*fn)(int,int,int,int,void *),void *arg){
    assert(fn && !arg);adds++;return add_result;
}
static void recovery_log(const char *event,int result,int http,const char *stage){
    assert(!in_callback);logs++;
    if(!strcmp(event,"APCTL event")) {
        last_error=result;last_event=http;snprintf(last_stage,sizeof(last_stage),"%s",stage);
    }
}
#include "wifi_events.h"
static void emit(int event,int error){
    in_callback=1;wifi_event_callback(3,0,event,error,NULL);in_callback=0;
}
int main(void){
    wifi_events_register();wifi_events_register();assert(adds==1);
    int before=logs;
    emit(6,(int)0x80410B05U);assert(logs==before);
    wifi_events_drain();assert(logs==before+1);
    assert((unsigned int)last_error==0x80410B05U && last_event==6);
    assert(strstr(last_stage,"event_ms=123456 old=3 new=0 error=0x80410B05 error"));
    emit(99,0);wifi_events_drain();assert(strstr(last_stage,"unknown event"));
    debug_enabled=0;before=logs;emit(6,-1);wifi_events_drain();assert(logs==before);debug_enabled=1;
    for(unsigned int i=0;i<WIFI_EVENT_CAPACITY+3;i++)emit(7,0);
    assert(wifi_event_dropped==3);
    wifi_events_drain();assert(wifi_event_read+60==wifi_event_write && wifi_event_reported_drops==3);
    while(wifi_event_read!=wifi_event_write)wifi_events_drain();
    wifi_event_read=wifi_event_write=UINT_MAX-1U;
    emit(1,0);emit(2,0);emit(3,0);wifi_events_drain();assert(wifi_event_read==wifi_event_write);
    wifi_events_terminated();add_result=-9;wifi_events_register();assert(adds==2 && wifi_event_handler==-9);
    add_result=8;wifi_events_register();assert(adds==3 && wifi_event_handler==8);
    wifi_events_register();assert(adds==3);
    return 0;
}

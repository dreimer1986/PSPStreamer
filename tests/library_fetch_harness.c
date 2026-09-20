#include <assert.h>
#include <string.h>
struct in_addr {unsigned int s_addr;};
static struct in_addr cached_server_address;
static int have_cached_server_address,remote_http_last_status,dns_calls,closed,http_calls,budget,dns_error,http_error;
static unsigned long long tick;
static char response[2048];
static const char *server_host="example.test";
static unsigned long long sceKernelGetSystemTimeWide(void){return tick;}
static int inet_aton(const char *name,struct in_addr *address){(void)name;(void)address;return 0;}
static int sceNetResolverCreate(int *id,void *work,int size){assert(work&&size==1024);*id=3;return 0;}
static int sceNetResolverInit(void){return 0;}
static int sceNetResolverStartNtoA(int id,const char *name,struct in_addr *address,unsigned timeout,int retries){
    assert(id==3&&!strcmp(name,server_host)&&timeout==2&&retries==1);
    dns_calls++;tick+=4000000;address->s_addr=1;return dns_error;
}
static int sceNetResolverDelete(int id){assert(id==3);closed++;return 0;}
static int remote_http_get_budget(const char *path,char *reply,int size,volatile int *running,int ms){
    assert(path&&reply==response&&size==sizeof(response)&&*running);http_calls++;budget=ms;return http_error;
}
#include "library_fetch.h"
int main(void){
    volatile int running=1;
    assert(library_fetch("/api/library",&running)==0);
    assert(dns_calls==1&&closed==1&&http_calls==1&&budget==11000);
    assert(library_fetch("/api/library",&running)==0&&budget==15000&&dns_calls==1);
    http_error=-1005;remote_http_last_status=401;
    assert(library_fetch("/api/library",&running)==-1003);
    remote_http_last_status=503;assert(library_fetch("/api/library",&running)==-1005);
    remote_http_last_status=408;assert(library_fetch("/api/library",&running)==-1005);
    have_cached_server_address=0;dns_error=-1;
    assert(library_fetch("/api/library",&running)==-1004&&closed==2&&!have_cached_server_address);
    int before=http_calls;running=0;
    assert(library_fetch("/api/library",&running)==-1005&&http_calls==before&&dns_calls==2);
}

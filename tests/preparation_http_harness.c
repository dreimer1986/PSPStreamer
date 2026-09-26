#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#define SO_NONBLOCK 123
#define SCE_NET_INET_POLLOUT 4
#define SCE_NET_INET_POLLIN 1
struct SceNetInetPollfd {int fd,events,revents;};
static unsigned long long tick;
static int scenario,server_https,server_port=80,have_cached_server_address=1,reads;
static struct in_addr cached_server_address;
static const char *server_host="test",*server_auth_header="";
static unsigned long long sceKernelGetSystemTimeWide(void){return tick;}
static int sceNetInetSocket(int a,int b,int c){(void)a;(void)b;(void)c;return 1;}
static int sceNetInetSetsockopt(int a,int b,int c,const void *d,int e){(void)a;(void)b;(void)c;(void)d;(void)e;return 0;}
static int sceNetInetConnect(int a,struct sockaddr *b,int c){(void)a;(void)b;(void)c;return -1;}
static int sceNetInetGetsockopt(int a,int b,int c,void *d,socklen_t *e){(void)a;(void)b;(void)c;(void)e;*(int *)d=0;return 0;}
static int sceNetInetPoll(struct SceNetInetPollfd *p,int n,int ms){
    (void)n;tick+=ms*1000;
    if(scenario==1)return 0;
    if(scenario==7 && reads && p->events==SCE_NET_INET_POLLIN)return 0;
    if(p->events==SCE_NET_INET_POLLIN && scenario==2 && tick<60000000ULL)return 0;
    p->revents=p->events;return 1;
}
static int sceNetInetSend(int a,const void *b,int c,int d){(void)a;(void)b;(void)d;return c;}
static int sceNetInetRecv(int a,void *b,int c,int d){
    (void)a;(void)d;
    reads++;
    if(scenario==4)return 0;
    const char *reply=scenario==7?"HTTP/1.0 200 OK\r\nContent-Length: 4\r\n\r\n{}":
        scenario==3?"HTTP/1.0 401 Denied\r\nContent-Length: 0\r\n\r\n":
        scenario==5?"HTTP/1.0 200 OK\r\nContent-Length: 999999\r\n\r\n":
        "HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\n{}";
    int n=strlen(reply);assert(n<c);memcpy(b,reply,n);return n;
}
static int tls_open(int fd,const char *host,int port,volatile int *running,int budget){
    (void)fd;(void)host;(void)port;(void)running;assert(budget<=15000);tick+=budget*1000;return -1;
}
static int tls_send(int a,const void *b,int c,volatile int *d,int e){(void)a;(void)b;(void)c;(void)d;(void)e;return -1;}
static int tls_recv(int a,void *b,int c,int d){(void)a;(void)b;(void)c;(void)d;return -1;}
static int connection_close(int fd){(void)fd;return 0;}
#include "remote_http.h"
int main(void){
    char buffer[256];volatile int running=1;RemoteHttpReport report;
    for(scenario=0;scenario<=7;scenario++) {
        tick=0;reads=0;server_https=scenario==6;
        int n=remote_http_request_policy("/subtitles",buffer,sizeof(buffer),&running,210000,NULL,&report);
        if(scenario==0 || scenario==2)assert(n==2 && report.status==200);
        else assert(n<0);
        if(scenario==1 || scenario==6)assert(tick<=15050000ULL && report.retryable && report.status==0);
        if(scenario==2)assert(tick>=60000000ULL); /* Cold subtitle build survives. */
        if(scenario==3)assert(report.status==401 && !report.retryable);
        if(scenario==4)assert(report.retryable);
        if(scenario==5)assert(!report.retryable);
        if(scenario==7)assert(tick>=30000000ULL && tick<31000000ULL && report.retryable && report.status==200 && !strcmp(report.stage,"response inactivity timeout"));
    }
    scenario=0;server_https=0;assert(remote_http_get("/control",buffer,sizeof(buffer),&running)==2);
    puts("preparation: 15s connect/TLS, 60s valid response, EOF retry, auth/protocol rejection: OK");
}

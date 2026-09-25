#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#define SceNetInetPollfd pollfd
#define SCE_NET_INET_POLLOUT POLLOUT
#define SCE_NET_INET_POLLIN POLLIN
#define SO_NONBLOCK 0x1009
#define sceNetInetSocket socket
#define sceNetInetConnect connect
#define sceNetInetGetsockopt getsockopt
#define sceNetInetSend send
#define sceNetInetRecv recv
#define sceNetInetPoll poll
static int closes;
static int sceNetInetClose(int fd) { closes++; return close(fd); }
static int sceNetInetSetsockopt(int fd,int level,int option,const void *value,int size) {
    assert(level==SOL_SOCKET && option==SO_NONBLOCK && size==sizeof(int) && *(int *)value==1);
    return fcntl(fd,F_SETFL,O_NONBLOCK);
}
static unsigned long long sceKernelGetSystemTimeWide(void) {
    struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
    return (unsigned long long)now.tv_sec*1000000+now.tv_nsec/1000;
}
static int have_cached_server_address=1,server_port;
static int server_https;
static int tls_open(int fd,const char *host,int port,volatile int *running,int timeout) {(void)fd;(void)host;(void)port;(void)running;(void)timeout;return -1;}
static int tls_send(int fd,const void *data,int size,volatile int *running,int timeout) {(void)fd;(void)data;(void)size;(void)running;(void)timeout;return -1;}
static int tls_recv(int fd,void *data,int size,int timeout) {(void)fd;(void)data;(void)size;(void)timeout;return -1;}
#define connection_close sceNetInetClose
static struct in_addr cached_server_address;
static const char *server_host="localhost";
static const char *server_auth_header="";
#include "remote_http.h"
int main(int argc,char **argv) {
    char result[2048]; volatile int running=1;
    assert(argc==3); server_port=atoi(argv[1]);
    cached_server_address.s_addr=htonl(INADDR_LOOPBACK);
    unsigned long long start=sceKernelGetSystemTimeWide();
    if(!strcmp(argv[2],"cancel")) running=0;
    char body[30001];memset(body,'x',30000);body[30000]=0;
    int n=!strcmp(argv[2],"post")?remote_http_request_budget("/api/comfort/sync",result,sizeof(result),&running,2000,body):
        remote_http_get("/api/remote/next?after=10",result,sizeof(result),&running);
    assert(closes==1 && remote_http_completed==1);
    assert(sceKernelGetSystemTimeWide()-start<2500000);
    if(!strcmp(argv[2],"ok") || !strcmp(argv[2],"lowercase") || !strcmp(argv[2],"post")) assert(n==2 && !strcmp(result,"{}"));
    else assert(n<0);
    if(!strcmp(argv[2],"unauthorized"))assert(remote_http_last_status==401);
    return 0;
}

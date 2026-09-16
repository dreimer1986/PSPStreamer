#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include "tls_host/pspnet_inet.h"
static volatile int download_running=1;
static volatile unsigned int download_bytes,download_total,download_speed;
static char download_error[160],server_host[]="127.0.0.1",server_auth_header[]="";
static int server_https,server_port;
#define PSP_O_WRONLY O_WRONLY
#define PSP_O_CREAT O_CREAT
#define PSP_SEEK_SET SEEK_SET
#define sceIoOpen open
#define sceIoLseek lseek
#define sceIoWrite write
#define sceIoClose close
#define sceNetInetSocket socket
#define sceNetInetConnect connect
#define sceNetInetGetsockopt getsockopt
#define connection_close close
static unsigned long long sceKernelGetSystemTimeWide(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (unsigned long long)t.tv_sec*1000000+t.tv_nsec/1000;}
static int prepare_server(struct sockaddr_in *p){memset(p,0,sizeof(*p));p->sin_family=AF_INET;p->sin_port=htons(server_port);p->sin_addr.s_addr=htonl(INADDR_LOOPBACK);return 0;}
static int tls_open(int a,const char*b,int c,volatile int*d,int e){(void)a;(void)b;(void)c;(void)d;(void)e;assert(0);return -1;}
static int tls_send(int a,const void*b,int c,volatile int*d,int e){(void)a;(void)b;(void)c;(void)d;(void)e;assert(0);return -1;}
static int stream_recv(int fd,void *data,int size,int timeout){struct pollfd p={fd,POLLIN,0};int n=poll(&p,1,timeout);return n==0?-2:n<0?-1:(int)recv(fd,data,size,0);}
static unsigned long long offline_size(const char *path){struct stat s;return stat(path,&s)?0:s.st_size;}
/* OFFLINE_HTTP */
int main(int argc,char **argv){
    assert(argc==6);server_port=atoi(argv[1]);unsigned int expected=strtoul(argv[4],NULL,10);
    int result=offline_http(argv[2],NULL,NULL,0,argv[3],expected);
    assert((result==0)==(atoi(argv[5])==0));
    if(result==0)assert(offline_size(argv[3])==expected);
    return 0;
}

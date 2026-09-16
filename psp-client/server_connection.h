/* Only the socket owner frees TLS. Configuration cannot change in playback. */
#include "tls_transport.h"
static int connection_connect(int fd,const struct sockaddr *address,int size) {
    if(!server_https)return sceNetInetConnect(fd,address,size);
    int nonblock=1,error=0; socklen_t length=sizeof(error);
    unsigned long long deadline=sceKernelGetSystemTimeWide()+15000000ULL;
    if(sceNetInetSetsockopt(fd,SOL_SOCKET,SO_NONBLOCK,&nonblock,sizeof(nonblock))<0)return -1;
    if(sceNetInetConnect(fd,address,size)<0) {
        for(;;) {
            struct SceNetInetPollfd p={fd,SCE_NET_INET_POLLOUT,0};
            int n=sceNetInetPoll(&p,1,50);
            if(n<0 || (unsigned long long)sceKernelGetSystemTimeWide()>=deadline)return -1;
            if(!n)continue;
            if(!(p.revents&SCE_NET_INET_POLLOUT) || sceNetInetGetsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&length)<0 || error)return -1;
            break;
        }
    }
    return tls_open(fd,server_host,server_port,NULL,15000);
}
static int connection_send(int fd,const void *data,int size,int flags) {
    return server_https?tls_send(fd,data,size,NULL,15000):(int)sceNetInetSend(fd,data,size,flags);
}
static int connection_recv(int fd,void *data,int size,int flags) {
    if(!server_https)return sceNetInetRecv(fd,data,size,flags);
    unsigned long long last=sceKernelGetSystemTimeWide();
    int n;
    do {n=tls_recv(fd,data,size,100);} while(n==-2 && sceKernelGetSystemTimeWide()-last<180000000ULL);
    return n;
}
static int connection_close(int fd) {return tls_close(fd);}

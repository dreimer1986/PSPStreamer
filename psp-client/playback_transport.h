/* Playback sockets stay nonblocking. Cancellation belongs to their owner;
 * never destroy a live TLS context from the UI thread. */
static int playback_connect(int fd,struct sockaddr_in *address,volatile int *running) {
    int nonblock=1,error=0;
    socklen_t length=sizeof(error);
    unsigned long long deadline=sceKernelGetSystemTimeWide()+15000000ULL;
    if(sceNetInetSetsockopt(fd,SOL_SOCKET,SO_NONBLOCK,&nonblock,sizeof(nonblock))<0)return -1;
    if(sceNetInetConnect(fd,(struct sockaddr *)address,sizeof(*address))<0) {
        for(;;) {
            struct SceNetInetPollfd p={fd,SCE_NET_INET_POLLOUT,0};
            if(!*running || (unsigned long long)sceKernelGetSystemTimeWide()>=deadline)return -1;
            int n=sceNetInetPoll(&p,1,50);
            if(n<0)return -1;
            if(!n)continue;
            if(!(p.revents&SCE_NET_INET_POLLOUT) ||
               sceNetInetGetsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&length)<0 || error)return -1;
            break;
        }
    }
    if(!*running)return -1;
    return server_https?tls_open(fd,server_host,server_port,running,15000):0;
}
static int playback_send(int fd,const void *buffer,int size,volatile int *running) {
    if(server_https)return tls_send(fd,buffer,size,running,15000);
    int sent=0;
    unsigned long long deadline=sceKernelGetSystemTimeWide()+15000000ULL;
    while(sent<size && *running && (unsigned long long)sceKernelGetSystemTimeWide()<deadline) {
        struct SceNetInetPollfd p={fd,SCE_NET_INET_POLLOUT,0};
        int ready=sceNetInetPoll(&p,1,50);
        if(ready<0)return -1;
        if(!ready)continue;
        if(!(p.revents&SCE_NET_INET_POLLOUT))return -1;
        int n=sceNetInetSend(fd,(const char *)buffer+sent,size-sent,0);
        if(n<0 && sceNetInetGetErrno()==35)continue;
        if(n<=0)return -1;
        sent+=n;
    }
    return sent==size?sent:-1;
}
static int playback_recv(int fd,void *buffer,int size,int timeout_ms) {
    if(server_https)return tls_recv(fd,buffer,size,timeout_ms);
    struct SceNetInetPollfd p={fd,SCE_NET_INET_POLLIN,0};
    int n=sceNetInetPoll(&p,1,timeout_ms);
    if(!n)return -2;
    if(n<0 || !(p.revents&SCE_NET_INET_POLLIN))return -1;
    n=sceNetInetRecv(fd,buffer,size,0);
    return n<0 && sceNetInetGetErrno()==35?-2:n;
}

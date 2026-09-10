/* Remote commands only. Never use this short, cancellable budget for media,
 * metadata or subtitle preparation. DNS was resolved by library/media setup. */
static volatile unsigned int remote_http_attempts, remote_http_completed;
static volatile int remote_http_last_result;
static const char * volatile remote_http_stage="idle";
static int remote_http_get(const char *path,char *buffer,int capacity,volatile int *running) {
    struct sockaddr_in server;
    char request[512];
    int fd=-1,result=-1005,nonblock=1,received=0,sent=0,header=-1,length=-1;
    unsigned long long deadline=sceKernelGetSystemTimeWide()+2000000ULL;
    if(capacity<2 || !have_cached_server_address) return -1004;
    remote_http_attempts++;
    remote_http_stage="connect";
    buffer[0]=0;
    memset(&server,0,sizeof(server));
    server.sin_family=AF_INET; server.sin_port=htons((unsigned short)server_port);
    server.sin_addr=cached_server_address;
    int wanted=snprintf(request,sizeof(request),"GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n",path,server_host);
    if(wanted<0 || wanted>=(int)sizeof(request)) { result=-1002; goto done; }
    fd=sceNetInetSocket(AF_INET,SOCK_STREAM,0);
    if(fd<0) { result=fd; goto done; }
    if(sceNetInetSetsockopt(fd,SOL_SOCKET,SO_NONBLOCK,&nonblock,sizeof(nonblock))<0) goto done;
    int connected=sceNetInetConnect(fd,(struct sockaddr *)&server,sizeof(server))>=0;
    while((!running || *running) && (unsigned long long)sceKernelGetSystemTimeWide()<deadline) {
        struct SceNetInetPollfd pollfd={fd,sent<wanted?SCE_NET_INET_POLLOUT:SCE_NET_INET_POLLIN,0};
        int ready=sceNetInetPoll(&pollfd,1,50);
        if(ready<0) goto done;
        if(!ready) continue;
        if(!connected) {
            int error=0; socklen_t size=sizeof(error);
            if(sceNetInetGetsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)<0 || error) goto done;
            connected=1;
        }
        if(sent<wanted) {
            remote_http_stage="send";
            if(!(pollfd.revents&SCE_NET_INET_POLLOUT)) goto done;
            int n=sceNetInetSend(fd,request+sent,wanted-sent,0);
            if(n<=0) goto done;
            sent+=n; continue;
        }
        if(!(pollfd.revents&SCE_NET_INET_POLLIN)) goto done;
        remote_http_stage="receive";
        int n=sceNetInetRecv(fd,buffer+received,capacity-1-received,0);
        if(n<=0) goto done;
        received+=n; buffer[received]=0;
        if(header<0) {
            char *body=strstr(buffer,"\r\n\r\n");
            if(body) {
                char *cl=strstr(buffer,"Content-Length:");
                if(strncmp(buffer,"HTTP/1.",7) || !strstr(buffer," 200 ") || !cl || cl>body) goto done;
                header=(int)(body+4-buffer); length=atoi(cl+15);
                if(length<0 || length>capacity-1-header) goto done;
            }
        }
        if(header>=0 && received>=header+length) {
            if(running && !*running) goto done;
            memmove(buffer,buffer+header,length); buffer[length]=0;
            result=length; break;
        }
        if(received==capacity-1) goto done;
    }
done:
    if(fd>=0) sceNetInetClose(fd); /* The worker owns its socket throughout cancellation. */
    remote_http_completed++;
    remote_http_last_result=result;
    remote_http_stage="idle";
    return result;
}

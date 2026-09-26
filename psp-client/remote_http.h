/* Bounded control-plane requests. Directory workers supply their own budget;
 * never use the short default for media, metadata or subtitle preparation. */
#include <strings.h>
static volatile unsigned int remote_http_attempts, remote_http_completed;
static volatile int remote_http_last_result;
static volatile int remote_http_last_status;
static const char * volatile remote_http_stage="idle";
typedef struct {volatile int status,retryable;const char *volatile stage;} RemoteHttpReport;
static void remote_http_phase(RemoteHttpReport *report,const char *stage) {
    remote_http_stage=stage;if(report)report->stage=stage;
}
static int remote_http_request_policy(const char *path,char *buffer,int capacity,volatile int *running,int budget_ms,const char *body,RemoteHttpReport *report) {
    struct sockaddr_in server;
    char request[2048];
    int fd=-1,result=-1005,nonblock=1,received=0,sent=0,header=-1,length=-1,tls_ready=0;
    int body_length=body?(int)strlen(body):0,body_sent=0;
    unsigned long long deadline=sceKernelGetSystemTimeWide()+(budget_ms?budget_ms*1000ULL:(server_https?15000000ULL:2000000ULL));
    remote_http_last_status=0;
    if(report) {
        report->status=0;report->retryable=1;report->stage="connect";
        deadline=sceKernelGetSystemTimeWide()+15000000ULL;
    }
    if(capacity<2 || !have_cached_server_address) return -1004;
    remote_http_attempts++;
    remote_http_phase(report,"connect");
    buffer[0]=0;
    memset(&server,0,sizeof(server));
    server.sin_family=AF_INET; server.sin_port=htons((unsigned short)server_port);
    server.sin_addr=cached_server_address;
    int wanted=body?snprintf(request,sizeof(request),"POST %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: %d\r\n%s\r\n",path,server_host,body_length,server_auth_header):
        snprintf(request,sizeof(request),"GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n%s\r\n",path,server_host, server_auth_header);
    if(wanted<0 || wanted>=(int)sizeof(request)) { result=-1002; goto done; }
    fd=sceNetInetSocket(AF_INET,SOCK_STREAM,0);
    if(fd<0) { result=fd; goto done; }
    if(sceNetInetSetsockopt(fd,SOL_SOCKET,SO_NONBLOCK,&nonblock,sizeof(nonblock))<0) goto done;
    int connected=sceNetInetConnect(fd,(struct sockaddr *)&server,sizeof(server))>=0;
    while((!running || *running) && (unsigned long long)sceKernelGetSystemTimeWide()<deadline) {
        struct SceNetInetPollfd pollfd={fd,sent<wanted || body_sent<body_length?SCE_NET_INET_POLLOUT:SCE_NET_INET_POLLIN,0};
        int ready;
        if(tls_ready && sent==wanted && body_sent==body_length) {ready=1;pollfd.revents=SCE_NET_INET_POLLIN;}
        else ready=sceNetInetPoll(&pollfd,1,50);
        if(ready<0) goto done;
        if(!ready) continue;
        if(!connected) {
            int error=0; socklen_t size=sizeof(error);
            if(sceNetInetGetsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)<0 || error) goto done;
            connected=1;
        }
        if(server_https && !tls_ready) {
            remote_http_phase(report,"TLS handshake");
            int remaining=(int)(((long long)deadline-(long long)sceKernelGetSystemTimeWide())/1000);
            if(remaining<=0)goto done;
            if(tls_open(fd,server_host,server_port,running,budget_ms?remaining:10000)<0)goto done;
            tls_ready=1;
            if(!budget_ms)deadline=sceKernelGetSystemTimeWide()+2000000ULL;
        }
        if(sent<wanted) {
            remote_http_phase(report,"send");
            if(!(pollfd.revents&SCE_NET_INET_POLLOUT)) goto done;
            int remaining=budget_ms?(int)(((long long)deadline-(long long)sceKernelGetSystemTimeWide())/1000):2000;
            if(remaining<=0)goto done;
            int n=server_https?tls_send(fd,request+sent,wanted-sent,running,remaining):(int)sceNetInetSend(fd,request+sent,wanted-sent,0);
            if(n<=0) goto done;
            sent+=n;
            if(report && sent==wanted && !body_length) {
                deadline=sceKernelGetSystemTimeWide()+budget_ms*1000ULL;
                remote_http_phase(report,"waiting for response");
            }
            continue;
        }
        if(body_sent<body_length) {
            if(!(pollfd.revents&SCE_NET_INET_POLLOUT))goto done;
            int remaining=(int)(((long long)deadline-(long long)sceKernelGetSystemTimeWide())/1000);
            if(remaining<=0)goto done;
            int n=server_https?tls_send(fd,body+body_sent,body_length-body_sent,running,remaining):(int)sceNetInetSend(fd,body+body_sent,body_length-body_sent,0);
            if(n<=0)goto done;
            body_sent+=n;
            if(report && body_sent==body_length)deadline=sceKernelGetSystemTimeWide()+budget_ms*1000ULL;
            continue;
        }
        if(!(pollfd.revents&SCE_NET_INET_POLLIN)) goto done;
        remote_http_phase(report,"receive");
        int n=server_https?tls_recv(fd,buffer+received,capacity-1-received,50):(int)sceNetInetRecv(fd,buffer+received,capacity-1-received,0);
        if(n==-2 && server_https)continue;
        if(n<=0) goto done;
        received+=n; buffer[received]=0;
        if(header<0) {
            char *body=strstr(buffer,"\r\n\r\n");
            if(body) {
                if(!strncmp(buffer,"HTTP/1.",7))remote_http_last_status=atoi(buffer+9);
                if(report) {
                    report->status=!strncmp(buffer,"HTTP/1.",7)?atoi(buffer+9):0;
                    report->retryable=report->status==200;
                }
                char *cl=strstr(buffer,"\r\n");
                while(cl && cl<body && strncasecmp(cl+2,"Content-Length:",15))cl=strstr(cl+2,"\r\n");
                if(strncmp(buffer,"HTTP/1.",7) || strncmp(buffer+9,"200 ",4))goto done;
                if(!cl || cl>=body) {if(report)report->retryable=0;goto done;}
                cl+=2;
                header=(int)(body+4-buffer); length=atoi(cl+15);
                if(length<0 || length>capacity-1-header) {if(report)report->retryable=0;goto done;}
            }
        }
        if(header>=0 && received>=header+length) {
            if(running && !*running) goto done;
            memmove(buffer,buffer+header,length); buffer[length]=0;
            result=length; break;
        }
        if(received==capacity-1) {if(report)report->retryable=0;goto done;}
    }
done:
    if(fd>=0) connection_close(fd); /* The worker owns its socket throughout cancellation. */
    remote_http_completed++;
    remote_http_last_result=result;
    remote_http_stage="idle";
    return result;
}
static int remote_http_request_budget(const char *path,char *buffer,int capacity,volatile int *running,int budget_ms,const char *body) {
    return remote_http_request_policy(path,buffer,capacity,running,budget_ms,body,NULL);
}
static int remote_http_get_budget(const char *path,char *buffer,int capacity,volatile int *running,int budget_ms) {
    return remote_http_request_budget(path,buffer,capacity,running,budget_ms,NULL);
}
static int remote_http_get(const char *path,char *buffer,int capacity,volatile int *running) {
    return remote_http_get_budget(path,buffer,capacity,running,0);
}

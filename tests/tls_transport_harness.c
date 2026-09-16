#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>
#include "tls_host/pspkernel.h"
#include "tls_host/pspnet_inet.h"
#include "tls_transport.h"
static pthread_mutex_t locks[8];static int count;
int sceKernelCreateSema(const char *name,int attr,int start,int maximum,void *opt) {
    (void)name;(void)attr;(void)opt;assert(start==1&&maximum==1&&count<8);
    pthread_mutex_init(&locks[count],NULL);return count++;
}
int sceKernelWaitSema(int id,int n,void *timeout) {(void)timeout;assert(n==1);return pthread_mutex_lock(&locks[id]);}
int sceKernelSignalSema(int id,int n) {assert(n==1);return pthread_mutex_unlock(&locks[id]);}
int main(int argc,char **argv) {
    assert(argc==3);int port=atoi(argv[1]);assert(tls_init()==0);
    int loops=!strcmp(argv[2],"rotate")?3:1;
    for(int i=0;i<loops;i++) {
        int fd=socket(AF_INET,SOCK_STREAM,0);assert(fd>=0);
        struct sockaddr_in address={0};address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        assert(connect(fd,(struct sockaddr *)&address,sizeof(address))==0);
        volatile int running=strcmp(argv[2],"cancel")!=0;
        int result=tls_open(fd,"localhost",port,&running,strcmp(argv[2],"timeout")?3000:150);
        if(!running || !strcmp(argv[2],"timeout")) {assert(result<0);tls_close(fd);continue;}
        assert(result==0);
        assert(tls_notice()==(!strcmp(argv[2],"storage")?3:i==0?1:i==1?0:2));
        tls_notice_clear();
        const char request[]="GET /api/health HTTP/1.0\r\nHost: localhost\r\n\r\n";
        assert(tls_send(fd,request,sizeof(request)-1,&running,1000)==sizeof(request)-1);
        char reply[4096];int used=0,n;
        /* Single-byte reads exercise already-decrypted buffered TLS data. */
        while(used<(int)sizeof(reply)-1) {n=tls_recv(fd,reply+used,1,100);if(n==-2)continue;if(n<=0)break;used+=n;}
        reply[used]=0;assert(strstr(reply,"200 OK") && strstr(reply,"\"ok\":true"));
        assert(tls_close(fd)==0);
    }
    return 0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later
 * Only the existing network command task owns these nonblocking descriptors.
 * No new tasks, rings, TLS contexts or allocations during normal streaming.
 * A PSP-side FTP plugin opts in. Silent clients lose descriptors after 20 s. */
#ifdef SM_TCP_SERVER_HOST
#include "../protocol.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
int sm_network_state(void);
int64_t esp_timer_get_time(void);
#else
#include "bridge.h"
#include "lwip/sockets.h"
#include "esp_timer.h"
#endif
#include "tcp_server.h"
#include <fcntl.h>
#include <errno.h>
typedef struct {int fd,listener;uint32_t token,peer;} Incoming;
static Incoming slots[4]={{-1,0,0,0},{-1,0,0,0},{-1,0,0,0},{-1,0,0,0}};
static int64_t activity;
static void release(Incoming *s){if(s->fd>=0)close(s->fd);*s=(Incoming){.fd=-1};}
static void reset(void){for(unsigned i=0;i<4;i++)release(&slots[i]);activity=0;}
void sm_tcp_server_idle(void){
    if(activity && (sm_network_state()!=SM_WIFI_READY || esp_timer_get_time()-activity>20000000))reset();
}
static Incoming *find(uint32_t token){
    for(unsigned i=0;i<4;i++)if(slots[i].fd>=0 && slots[i].token==token)return &slots[i];
    return NULL;
}
static Incoming *vacant(uint32_t token){
    if(!token || find(token))return NULL;
    for(unsigned i=0;i<4;i++)if(slots[i].fd<0)return &slots[i];
    return NULL;
}
static int nonblock(int fd){int flags=fcntl(fd,F_GETFL,0);return flags>=0?fcntl(fd,F_SETFL,flags|O_NONBLOCK):-1;}
void sm_tcp_server_command(const SmFrame *r,SmFrame *out){
    out->result=SM_INVALID;
    if(r->op==SM_TCP_RESET){if(!r->length){reset();out->result=0;}return;}
    if(sm_network_state()!=SM_WIFI_READY){reset();out->result=SM_OFFLINE;return;}
    activity=esp_timer_get_time();
    if(r->op==SM_TCP_LISTEN){
        if(r->length!=sizeof(SmTcpListen))return;
        SmTcpListen p;memcpy(&p,r->payload,sizeof(p));
        if(p.port>65535)return;
        Incoming *s=vacant(p.token);if(!s){out->result=SM_BUSY;return;}
        int fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(fd<0){out->result=SM_IO;return;}
        int reuse=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
        struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons(p.port),.sin_addr.s_addr=INADDR_ANY};
        socklen_t n=sizeof(a);
        if(nonblock(fd)<0 || bind(fd,(void *)&a,sizeof(a))<0 || listen(fd,1)<0 || getsockname(fd,(void *)&a,&n)<0){close(fd);out->result=SM_IO;return;}
        *s=(Incoming){fd,1,p.token,p.peer};
        SmTcpAddress reply={ntohs(a.sin_port),0};memcpy(out->payload,&reply,sizeof(reply));out->length=sizeof(reply);out->result=0;return;
    }
    if(r->op==SM_TCP_ACCEPT){
        if(r->length!=sizeof(SmTcpAccept))return;
        SmTcpAccept p;memcpy(&p,r->payload,sizeof(p));
        Incoming *l=find(p.listener),*s=vacant(p.token);
        if(!l || !l->listener || !s)return;
        struct sockaddr_in a;socklen_t n=sizeof(a);
        int fd=accept(l->fd,(void *)&a,&n);
        if(fd<0){out->result=(errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR)?SM_BUSY:SM_IO;return;}
        if((l->peer && l->peer!=a.sin_addr.s_addr) || nonblock(fd)<0){close(fd);out->result=SM_BUSY;return;}
        *s=(Incoming){fd,0,p.token,a.sin_addr.s_addr};
        SmTcpAddress reply={ntohs(a.sin_port),a.sin_addr.s_addr};
        memcpy(out->payload,&reply,sizeof(reply));out->length=sizeof(reply);out->result=0;return;
    }
    if(r->length<sizeof(SmSocketRequest))return;
    SmSocketRequest p;memcpy(&p,r->payload,sizeof(p));Incoming *s=find(p.token);
    if(!s){out->result=SM_OFFLINE;return;}
    if(r->op==SM_TCP_CLOSE){if(r->length==sizeof(p)){release(s);out->result=0;}return;}
    if(s->listener)return;
    int n;
    if(r->op==SM_TCP_READ){
        if(r->length!=sizeof(p) || !p.length || p.length>SM_PAYLOAD_SIZE)return;
        n=recv(s->fd,out->payload,p.length,0);
        if(n>=0){out->length=n;out->result=0;return;}
    }else if(r->op==SM_TCP_WRITE){
        if(!p.length || p.length>SM_PAYLOAD_SIZE-sizeof(p) || r->length!=sizeof(p)+p.length)return;
        n=send(s->fd,r->payload+sizeof(p),p.length,0);
        if(n>=0){out->result=n;return;}
    }else return;
    out->result=(errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR)?SM_BUSY:SM_IO;
}

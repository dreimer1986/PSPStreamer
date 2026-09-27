/* SPDX-License-Identifier: GPL-2.0-or-later
 * Application-local virtual sockets. Never hooks another PSP application's
 * network stack. A USB RPC is short and serialized; TCP owners are independent. */
#include <pspkernel.h>
#include <pspsdk.h>
#include <pspnet_apctl.h>
#include <kubridge.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "streammaster_transport.h"
#define FD_BASE 0x60000000
#define LOCAL_SOCKETS 12
typedef struct {
    int fd,nonblock,opened,eof,dead;
    unsigned token,epoch,pos,end;
    SmSocketOpen destination;
    unsigned char cache[SM_PAYLOAD_SIZE];
} LocalSocket;
static LocalSocket sockets[LOCAL_SOCKETS];
static SceUID rpc_lock=-1,slots_lock=-1,module=-1;
static volatile int selected,broken=1,wifi_state;
static unsigned capable_generation;
static unsigned sequence,generation=1,next_token=1,next_fd=1;
static SmFrame request,response;
static char target_host[128];
static int target_port,target_tls;
static const char *phase="idle";
static int lock(SceUID id,int milliseconds,volatile int *running) {
    SceInt64 end=sceKernelGetSystemTimeWide()+(SceInt64)milliseconds*1000;
    while(!running || *running) {
        SceUInt wait=10000;
        if(sceKernelWaitSema(id,1,&wait)>=0)return 0;
        if(sceKernelGetSystemTimeWide()>=end)break;
    }
    return SM_BUSY;
}
const char *stm_stage(void){return phase;}
int stm_enabled(void){return selected;}
void stm_server(const char *host,int port,int https) {
    if(slots_lock<0 || lock(slots_lock,100,NULL)<0)return;
    snprintf(target_host,sizeof(target_host),"%s",host);target_port=port;target_tls=https;
    sceKernelSignalSema(slots_lock,1);
}
int stm_init(int enabled,const char *host,int port,int https) {
    selected=enabled;
    if(rpc_lock<0)rpc_lock=sceKernelCreateSema("STM USB",0,1,1,NULL);
    if(slots_lock<0)slots_lock=sceKernelCreateSema("STM sockets",0,1,1,NULL);
    if(rpc_lock<0 || slots_lock<0)return SM_IO;
    next_token=(unsigned)sceKernelGetSystemTimeWide()|1;
    stm_server(host,port,https);return 0;
}
int stm_rpc(unsigned op,const void *data,unsigned size,void *reply,unsigned capacity,unsigned *length,volatile int *running) {
    if(length)*length=0;
    if(size>SM_PAYLOAD_SIZE || rpc_lock<0 || (op>=SM_SOCKET_OPEN && broken))return SM_OFFLINE;
    if(lock(rpc_lock,1000,running)<0)return SM_BUSY;
    if(module<0 || (op>=SM_SOCKET_OPEN && broken)){sceKernelSignalSema(rpc_lock,1);return SM_OFFLINE;}
    memset(&request,0,sizeof(request));memset(&response,0,sizeof(response));
    request.op=op;request.sequence=++sequence;request.length=size;
    if(size)memcpy(request.payload,data,size);
    sm_seal(&request);
    int rc=sceIoDevctl("stm:",SM_DEV_EXCHANGE,&request,sizeof(request),&response,sizeof(response));
    if(rc<0 && rc!=SM_BUSY){broken=1;wifi_state=SM_WIFI_FAILED;generation++;}
    if(rc>=0) {
        if(response.length>capacity || (response.length && !reply))rc=SM_INVALID;
        else {if(reply && response.length)memcpy(reply,response.payload,response.length);if(length)*length=response.length;rc=response.result;}
    }
    memset(&request,0,sizeof(request));memset(&response,0,sizeof(response));
    sceKernelSignalSema(rpc_lock,1);return rc;
}
static int load_driver(void) {
    if(module>=0)return 0;
    phase="load Sony USB";
    int m=kuKernelLoadModule("flash0:/kd/usb.prx",0,NULL),status=0,rc;
    if(m>=0) {
        phase="start Sony USB";rc=sceKernelStartModule(m,0,NULL,&status,NULL);
        if(rc<0 || status<0)return rc<0?rc:status;
    } else if((unsigned)m!=0x80020139U)return m;
    char cwd[192],path[256];
    if(!getcwd(cwd,sizeof(cwd)))snprintf(cwd,sizeof(cwd),"ms0:/PSP/GAME/PSPStreamer");
    snprintf(path,sizeof(path),"%s/StreamMasterUSB.prx",cwd);
    phase="load bridge PRX";m=kuKernelLoadModule(path,0,NULL);if(m<0)return m;
    phase="start bridge PRX";rc=sceKernelStartModule(m,0,NULL,&status,NULL);
    if(rc<0 || status<0){sceKernelUnloadModule(m);return rc<0?rc:status;}
    module=m;return 0;
}
int stm_driver_start(int force,volatile int *running) {
    if(lock(rpc_lock,1000,running)<0)return SM_BUSY;
    int rc=load_driver();
    if(rc>=0 && !force && !broken && sceIoDevctl("stm:",SM_DEV_STATUS,NULL,0,NULL,0)>0) {
        sceKernelSignalSema(rpc_lock,1);return 0;
    }
    if(rc>=0) {
        phase="reset USB driver";rc=sceIoDevctl("stm:",SM_DEV_STOP,NULL,0,NULL,0);
        generation++;broken=1;
    }
    if(rc>=0){phase="activate USB driver";rc=sceIoDevctl("stm:",SM_DEV_START,NULL,0,NULL,0);}
    if(rc>=0) {
        phase="wait USB attach";SceInt64 end=sceKernelGetSystemTimeWide()+10000000;
        do {
            if(running && !*running){rc=SM_TIMEOUT;break;}
            rc=sceIoDevctl("stm:",SM_DEV_STATUS,NULL,0,NULL,0);
            if(rc>0){broken=0;rc=0;break;}
            if(rc<0)break;
            sceKernelDelayThread(20000);
        }while(sceKernelGetSystemTimeWide()<end);
        if(broken && rc>=0)rc=SM_TIMEOUT;
    }
    sceKernelSignalSema(rpc_lock,1);return rc;
}
void stm_driver_cancel(void) {if(module>=0)sceIoDevctl("stm:",SM_DEV_CANCEL,NULL,0,NULL,0);broken=1;generation++;}
void stm_driver_stop(void) {
    if(rpc_lock<0)return;
    stm_driver_cancel();
    if(lock(rpc_lock,1000,NULL)<0)return;
    if(module>=0) {
        int rc=sceIoDevctl("stm:",SM_DEV_STOP,NULL,0,NULL,0),status=0;
        if(rc>=0 && sceKernelStopModule(module,0,NULL,&status,NULL)>=0 && status>=0 && sceKernelUnloadModule(module)>=0)module=-1;
    }
    wifi_state=SM_WIFI_IDLE;sceKernelSignalSema(rpc_lock,1);
}
int stm_associate(volatile int *running,int force) {
    int reset=force || broken || capable_generation!=generation;
    int rc=stm_driver_start(force,running);if(rc<0)return rc;
    phase="StreamMaster firmware capability";
    /* Called after playback owners stop. RESET closes stale ESP connections. */
    if(reset){rc=stm_rpc(SM_SOCKET_RESET,NULL,0,NULL,0,NULL,running);if(rc<0)return rc;capable_generation=generation;}
    SceInt64 end=sceKernelGetSystemTimeWide()+30000000;int reconnect_sent=0;
    if(force) {
        phase="Onju Wi-Fi reconnect";
        rc=stm_rpc(SM_CONNECT,NULL,0,NULL,0,NULL,running);
        if(rc<0)return rc;
        reconnect_sent=1;
    }
    while(!running || *running) {
        SmInfo info;unsigned length=0;phase="Onju Wi-Fi";
        rc=stm_rpc(SM_INFO,NULL,0,&info,sizeof(info),&length,running);
        if(rc<0)return rc;
        if(length!=sizeof(info))return SM_IO;
        wifi_state=info.wifi_state;
        if(wifi_state==SM_WIFI_READY)return 0;
        if(!reconnect_sent && wifi_state!=SM_WIFI_CONNECTING) {
            rc=stm_rpc(SM_CONNECT,NULL,0,NULL,0,NULL,running);if(rc<0)return rc;reconnect_sent=1;
        }
        if(sceKernelGetSystemTimeWide()>=end)return SM_TIMEOUT;
        sceKernelDelayThread(100000);
    }
    return SM_TIMEOUT;
}
static LocalSocket *get(int fd) {
    for(int i=0;i<LOCAL_SOCKETS;i++)if(sockets[i].fd==fd)return &sockets[i];
    return NULL;
}
static int virtual_fd(int fd){return fd>=FD_BASE;}
int stm_socket(int domain,int type,int protocol) {
    if(!selected)return sceNetInetSocket(domain,type,protocol);
    if(domain!=AF_INET || type!=SOCK_STREAM || broken || lock(slots_lock,100,NULL)<0){errno=5;return -1;}
    int fd=-1;
    for(int i=0;i<LOCAL_SOCKETS;i++)if(!sockets[i].fd) {
        LocalSocket *s=&sockets[i];memset(s,0,sizeof(*s));
        s->fd=fd=FD_BASE+(next_fd++&0xfffffff);s->token=++next_token;if(!s->token)s->token=++next_token;
        s->epoch=generation;s->destination.token=s->token;s->destination.port=target_port;s->destination.tls=target_tls;
        snprintf(s->destination.host,sizeof(s->destination.host),"%s",target_host);break;
    }
    sceKernelSignalSema(slots_lock,1);errno=fd<0?24:0;return fd;
}
static int socket_status(LocalSocket *s,SmSocketStatus *status) {
    if(!s || !s->opened || s->dead || s->epoch!=generation || broken)return -1;
    SmSocketRequest r={s->token,0};unsigned length;
    int rc=stm_rpc(SM_SOCKET_STATUS,&r,sizeof(r),status,sizeof(*status),&length,NULL);
    if(rc==SM_BUSY)return 0;
    return rc<0 || length!=sizeof(*status)?-1:1;
}
int stm_connect(int fd,const struct sockaddr *address,socklen_t size) {
    if(!virtual_fd(fd))return sceNetInetConnect(fd,address,size);
    LocalSocket *s=get(fd);if(!s || s->dead || broken){errno=5;return -1;}
    int rc=stm_rpc(SM_SOCKET_OPEN,&s->destination,sizeof(s->destination),NULL,0,NULL,NULL);
    if(rc<0){errno=rc==SM_BUSY?35:5;return -1;}
    s->opened=1;
    if(s->nonblock){errno=119;return -1;}
    struct SceNetInetPollfd p={fd,SCE_NET_INET_POLLOUT,0};
    return stm_poll(&p,1,15000)>0 && (p.revents&SCE_NET_INET_POLLOUT)?0:-1;
}
int stm_poll(struct SceNetInetPollfd *fds,size_t count,int timeout) {
    int any=0;for(size_t i=0;i<count;i++)if(virtual_fd(fds[i].fd))any=1;
    if(!any)return sceNetInetPoll(fds,count,timeout);
    SceInt64 end=sceKernelGetSystemTimeWide()+(SceInt64)(timeout<0?15000:timeout)*1000;
    do {
        int ready=0;
        for(size_t i=0;i<count;i++) {
            struct SceNetInetPollfd *p=&fds[i];p->revents=0;
            if(!virtual_fd(p->fd)){if(sceNetInetPoll(p,1,0)>0)ready++;continue;}
            LocalSocket *s=get(p->fd);
            if(!s || s->epoch!=generation || s->dead || broken)p->revents=SCE_NET_INET_POLLERR;
            else if(s->end>s->pos && (p->events&SCE_NET_INET_POLLIN))p->revents|=SCE_NET_INET_POLLIN;
            else {
                SmSocketStatus status={0};int rc=socket_status(s,&status);
                if(rc<0)p->revents=SCE_NET_INET_POLLERR;
                else if(rc>0) {
                    if((p->events&SCE_NET_INET_POLLIN) && (status.available || status.state==SM_SOCKET_EOF))p->revents|=SCE_NET_INET_POLLIN;
                    if((p->events&SCE_NET_INET_POLLOUT) && status.state==SM_SOCKET_READY && status.space>=SM_PAYLOAD_SIZE)p->revents|=SCE_NET_INET_POLLOUT;
                    if(status.state==SM_SOCKET_ERROR && !status.available)p->revents|=SCE_NET_INET_POLLERR;
                }
            }
            if(p->revents)ready++;
        }
        if(ready)return ready;
        if(sceKernelGetSystemTimeWide()>=end)break;
        sceKernelDelayThread(5000);
    }while(1);
    return 0;
}
int stm_setsockopt(int fd,int level,int option,const void *value,socklen_t size) {
    if(!virtual_fd(fd))return sceNetInetSetsockopt(fd,level,option,value,size);
    LocalSocket *s=get(fd);if(!s)return -1;
    if(level==SOL_SOCKET && option==SO_NONBLOCK && size>=sizeof(int))s->nonblock=*(const int *)value!=0;
    return 0;
}
int stm_getsockopt(int fd,int level,int option,void *value,socklen_t *size) {
    if(!virtual_fd(fd))return sceNetInetGetsockopt(fd,level,option,value,size);
    if(level!=SOL_SOCKET || option!=SO_ERROR || !size || *size<sizeof(int))return -1;
    SmSocketStatus status={0};int rc=socket_status(get(fd),&status);
    *(int *)value=rc>0 && status.state==SM_SOCKET_READY?0:5;*size=sizeof(int);return 0;
}
size_t stm_send(int fd,const void *data,size_t size,int flags) {
    if(!virtual_fd(fd))return sceNetInetSend(fd,data,size,flags);
    LocalSocket *s=get(fd);if(!s || s->dead || broken || s->epoch!=generation){errno=5;return (size_t)-1;}
    if(size>SM_PAYLOAD_SIZE-sizeof(SmSocketRequest))size=SM_PAYLOAD_SIZE-sizeof(SmSocketRequest);
    unsigned char buffer[SM_PAYLOAD_SIZE];SmSocketRequest r={s->token,size};
    memcpy(buffer,&r,sizeof(r));memcpy(buffer+sizeof(r),data,size);
    int rc=stm_rpc(SM_SOCKET_WRITE,buffer,sizeof(r)+size,NULL,0,NULL,NULL);
    /* USB arbitration can briefly be busy even after POLLOUT. Retry only
     * unsent writes; a completed WRITE is never submitted a second time. */
    for(int retry=0;rc==SM_BUSY && retry<3;retry++) {
        sceKernelDelayThread(1000);
        rc=stm_rpc(SM_SOCKET_WRITE,buffer,sizeof(r)+size,NULL,0,NULL,NULL);
    }
    errno=rc==SM_BUSY?35:rc<0?5:0;
    return rc<0?(size_t)-1:(size_t)rc;
}
size_t stm_recv(int fd,void *data,size_t size,int flags) {
    if(!virtual_fd(fd))return sceNetInetRecv(fd,data,size,flags);
    LocalSocket *s=get(fd);if(!s || s->dead || broken || s->epoch!=generation){errno=5;return (size_t)-1;}
    if(!size)return 0;
    if(s->pos==s->end) {
        if(s->eof)return 0;
        SmSocketRequest r={s->token,SM_PAYLOAD_SIZE};unsigned length=0;
        int rc=stm_rpc(SM_SOCKET_READ,&r,sizeof(r),s->cache,sizeof(s->cache),&length,NULL);
        if(rc<0){errno=rc==SM_BUSY?35:5;return (size_t)-1;}
        s->pos=0;s->end=length;if(!length){s->eof=1;return 0;}
    }
    if(size>s->end-s->pos)size=s->end-s->pos;
    memcpy(data,s->cache+s->pos,size);s->pos+=size;return size;
}
int stm_errno(void){return selected?errno:sceNetInetGetErrno();}
int stm_apstate(int *state) {
    if(!selected)return sceNetApctlGetState(state);
    *state=!broken && wifi_state==SM_WIFI_READY?PSP_NET_APCTL_STATE_GOT_IP:0;return 0;
}
int stm_tls_open(int fd,const char *host,int port,volatile int *running,int timeout) {
    if(!virtual_fd(fd))return tls_open(fd,host,port,running,timeout);
    LocalSocket *s=get(fd);return s && !s->dead && !broken && (!running || *running)?0:-1;
}
int stm_tls_recv(int fd,void *data,int size,int timeout) {
    if(!virtual_fd(fd))return tls_recv(fd,data,size,timeout);
    struct SceNetInetPollfd p={fd,SCE_NET_INET_POLLIN,0};int rc=stm_poll(&p,1,timeout);
    if(!rc)return -2;
    if(rc<0 || !(p.revents&SCE_NET_INET_POLLIN))return -1;
    int n=(int)stm_recv(fd,data,size,0);
    return n<0 && errno==35?-2:n;
}
int stm_tls_send(int fd,const void *data,int size,volatile int *running,int timeout) {
    if(!virtual_fd(fd))return tls_send(fd,data,size,running,timeout);
    int sent=0;SceInt64 end=sceKernelGetSystemTimeWide()+(SceInt64)timeout*1000;
    while(sent<size && (!running || *running) && sceKernelGetSystemTimeWide()<end) {
        struct SceNetInetPollfd p={fd,SCE_NET_INET_POLLOUT,0};int rc=stm_poll(&p,1,50);
        if(rc<0 || (p.revents&SCE_NET_INET_POLLERR))return -1;
        if(!rc)continue;
        int n=(int)stm_send(fd,(const char *)data+sent,size-sent,0);
        if(n<0 && errno==35)continue;
        if(n<=0)return -1;
        sent+=n;
    }
    return sent==size?sent:-1;
}
int stm_close(int fd) {
    if(!virtual_fd(fd))return tls_close(fd);
    LocalSocket *s=get(fd);if(!s)return -1;
    if(s->opened && !broken && s->epoch==generation) {
        SmSocketRequest r={s->token,0};
        int rc=SM_BUSY;
        for(int i=0;i<3 && rc==SM_BUSY;i++)rc=stm_rpc(SM_SOCKET_CLOSE,&r,sizeof(r),NULL,0,NULL,NULL);
        if(rc<0 && rc!=SM_OFFLINE){broken=1;generation++;}
    }
    if(lock(slots_lock,1000,NULL)<0)return -1;
    memset(s,0,sizeof(*s));sceKernelSignalSema(slots_lock,1);return 0;
}

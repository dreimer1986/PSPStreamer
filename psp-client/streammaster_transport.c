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
#include "streammaster_transport.h"
#define FD_BASE 0x60000000
#define LOCAL_SOCKETS 12
typedef struct {
    int fd,nonblock,opened,eof,dead,opening,connected;
    unsigned token,epoch,pos,end;
    unsigned read_backoff;
    SceInt64 read_retry,open_retry,diagnostic_next,empty_since;
    SmSocketOpen destination;
    SmBulkResult buffers[2];
    unsigned cache_index;
    unsigned ahead_length;
    int ahead_ready,ahead_result;
    unsigned samples,empty_samples,full_samples,max_available,groups,group_bytes,short_groups;
    unsigned long long finish_us;
} LocalSocket;
static LocalSocket sockets[LOCAL_SOCKETS];
/* PSP newlib's __errno uses the shared _impure_ptr in this toolchain.
 * Do not let the remote-control worker replace a media owner's EWOULDBLOCK.
 * No per-request allocation: owners release these bounded slots on exit. */
static struct {volatile int tid,code;} thread_errors[32];
static void socket_error(int code) {
    int tid=sceKernelGetThreadId();
    for(unsigned i=0;i<32;i++)if(thread_errors[i].tid==tid){thread_errors[i].code=code;return;}
    for(unsigned i=0;i<32;i++)if(__sync_bool_compare_and_swap(&thread_errors[i].tid,0,tid)) {
        thread_errors[i].code=code;return;
    }
}
void stm_thread_finished(void) {
    int tid=sceKernelGetThreadId();
    for(unsigned i=0;i<32;i++)if(thread_errors[i].tid==tid) {
        thread_errors[i].code=0;__sync_synchronize();thread_errors[i].tid=0;return;
    }
}
static SceUID rpc_lock=-1,slots_lock=-1,module=-1;
static volatile int selected,broken=1,wifi_state;
static int compact_packets;
static int bulk_pairs,ahead_bulk;
static unsigned peer_caps,peer_caps_length;
static int peer_caps_result=SM_OFFLINE,bridge_bulk_result=SM_OFFLINE;
static unsigned capable_generation;
static unsigned sequence,generation=1,next_token=1,next_fd=1;
static SmFrame request,response;
/* rpc_lock owns a legacy transaction or a two-request bulk group and mailboxes. */
static int ahead_supported=1;
static unsigned ahead_token,ahead_epoch;
static char target_host[128];
static int target_port,target_tls;
static const char *phase="idle";
static int diagnostic_enabled;
static struct {
    unsigned calls,reads,bytes,busy,errors,max_us,slow_op,prefetched;
    unsigned samples,empty,full,min,max;
    unsigned long long usb_us,wait_us,start;
} diagnostic;
void stm_diagnostic_enable(int enabled){diagnostic_enabled=enabled;}
int stm_diagnostic_snapshot(char *line,unsigned size,int buffers) {
    if(!selected || !diagnostic_enabled || rpc_lock<0)return 0;
    SceUInt wait=1000;
    if(sceKernelWaitSema(rpc_lock,1,&wait)<0)return 0;
    unsigned long long elapsed=sceKernelGetSystemTimeWide()-diagnostic.start;
    if(buffers)snprintf(line,size,"bulk=%d caps=%x probe=%d len=%u bridge=%d span_ms=%llu rx_B=%u samples=%u empty=%u full=%u min_B=%u max_B=%u compact=%d ahead=%u cumulative",
        bulk_pairs,peer_caps,peer_caps_result,peer_caps_length,bridge_bulk_result,
        elapsed/1000,diagnostic.bytes,diagnostic.samples,diagnostic.empty,diagnostic.full,diagnostic.min,diagnostic.max,compact_packets,diagnostic.prefetched);
    else snprintf(line,size,"calls=%u reads=%u KiB_s=%u usb_ms=%llu lock_ms=%llu max_us=%u op=%u busy=%u err=%u",
        diagnostic.calls,diagnostic.reads,elapsed?(unsigned)((unsigned long long)diagnostic.bytes*1000000/elapsed/1024):0,
        diagnostic.usb_us/1000,diagnostic.wait_us/1000,diagnostic.max_us,diagnostic.slow_op,diagnostic.busy,diagnostic.errors);
    sceKernelSignalSema(rpc_lock,1);return 1;
}
static int lock(SceUID id,int milliseconds,volatile int *running) {
    SceInt64 end=sceKernelGetSystemTimeWide()+(SceInt64)milliseconds*1000;
    while(!running || *running) {
        SceUInt wait=10000;
        if(sceKernelWaitSema(id,1,&wait)>=0)return 0;
        if(sceKernelGetSystemTimeWide()>=end)break;
    }
    return SM_BUSY;
}
static void finish_ahead(void) {
    if(!ahead_token)return;
    LocalSocket *owner=NULL;
    for(int i=0;i<LOCAL_SOCKETS;i++)if(sockets[i].fd && sockets[i].token==ahead_token && sockets[i].epoch==ahead_epoch){owner=&sockets[i];break;}
    /* A closed slot is drained before reuse. Reset clears ahead_token. */
    if(!owner){broken=1;generation++;ahead_token=0;return;}
    SmBulkResult *packet=&owner->buffers[1-owner->cache_index];
    unsigned long long started=diagnostic_enabled?sceKernelGetSystemTimeWide():0;
    int rc=ahead_bulk?sceIoDevctl("stm:",SM_DEV_BULK_FINISH,NULL,0,packet,sizeof(*packet)):
        sceIoDevctl("stm:",SM_DEV_READ_FINISH,NULL,0,&response,sizeof(response));
    if(!ahead_bulk && rc>=0) {
        packet->length=response.length;packet->result=response.result;
        if(packet->length<=sizeof(packet->payload))memcpy(packet->payload,response.payload,packet->length);
    }
    if(rc>=0 && packet->length>sizeof(packet->payload))rc=SM_IO;
    if(rc<0){broken=1;wifi_state=SM_WIFI_FAILED;generation++;}
    if(diagnostic_enabled) {
        owner->groups++;owner->finish_us+=sceKernelGetSystemTimeWide()-started;
        if(rc>=0 && packet->result>=0)owner->group_bytes+=packet->length;
        if(rc>=0 && packet->length<(ahead_bulk?SM_PAIR_PAYLOAD_SIZE:SM_PAYLOAD_SIZE))owner->short_groups++;
        diagnostic.calls++;diagnostic.reads++;
        diagnostic.prefetched++;
        /* Only blocked finish time; the rest overlaps application work. */
        diagnostic.usb_us+=sceKernelGetSystemTimeWide()-started;
        if(rc>=0 && packet->result>=0)diagnostic.bytes+=packet->length;
        if(rc<0 || packet->result<0) {
            if(rc>=0 && packet->result==SM_BUSY)diagnostic.busy++;else diagnostic.errors++;
        }
    }
    for(int i=0;i<LOCAL_SOCKETS;i++) {
        LocalSocket *s=&sockets[i];
        if(s->fd && s->token==ahead_token && s->epoch==ahead_epoch && s->epoch==generation) {
            s->ahead_result=rc<0?rc:packet->result;
            s->ahead_length=rc>=0 && packet->result>=0?packet->length:0;
            s->ahead_ready=1;break;
        }
    }
    ahead_token=0;
}
static int take_ahead(LocalSocket *s,int *result,unsigned *length) {
    if(lock(rpc_lock,30,NULL)<0)return -1;
    finish_ahead();
    int ready=s->ahead_ready;
    if(ready) {
        *result=s->ahead_result;*length=s->ahead_length;
        s->cache_index=1-s->cache_index; /* Exchange ownership, not payload bytes. */
        s->ahead_ready=0;
    }
    sceKernelSignalSema(rpc_lock,1);return ready;
}
static void start_ahead(LocalSocket *s) {
    /* Do not block playback/storage to obtain speculative data. */
    SceUInt wait=1;
    if(!ahead_supported || !compact_packets || sceKernelWaitSema(rpc_lock,1,&wait)<0)return;
    if(!ahead_token && !s->ahead_ready && !broken && s->epoch==generation && !s->eof && !s->dead) {
        SmSocketRequest r={s->token,bulk_pairs?SM_BULK_PAYLOAD_SIZE:SM_PAYLOAD_SIZE};
        memset(&request,0,32+sizeof(r));request.op=bulk_pairs?SM_SOCKET_READ_BULK:SM_SOCKET_READ;
        request.sequence=++sequence;request.length=sizeof(r);memcpy(request.payload,&r,sizeof(r));sm_seal(&request);
        if(bulk_pairs)sequence++;
        int rc=sceIoDevctl("stm:",bulk_pairs?SM_DEV_BULK_BEGIN:SM_DEV_READ_BEGIN,&request,sizeof(request),NULL,0);
        if(rc==SM_INVALID)ahead_supported=0; /* Old bridge: retain synchronous reads. */
        else if(rc>=0){ahead_token=s->token;ahead_epoch=s->epoch;ahead_bulk=bulk_pairs;}
        else if(rc!=SM_BUSY){broken=1;wifi_state=SM_WIFI_FAILED;generation++;}
    }
    sceKernelSignalSema(rpc_lock,1);
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
    unsigned long long entered=diagnostic_enabled?sceKernelGetSystemTimeWide():0;
    if(length)*length=0;
    if(size>SM_PAYLOAD_SIZE || rpc_lock<0 || (op>=SM_SOCKET_OPEN && broken))return SM_OFFLINE;
    /* Data/control pollers yield promptly to the current owner. Closing must
     * get a longer chance to release its remote slot before declaring failure. */
    if(lock(rpc_lock,op>=SM_SOCKET_OPEN && op!=SM_SOCKET_CLOSE?30:1000,running)<0)return SM_BUSY;
    finish_ahead();
    if(module<0 || (op>=SM_SOCKET_OPEN && broken)){sceKernelSignalSema(rpc_lock,1);return SM_OFFLINE;}
    memset(&request,0,sizeof(request));memset(&response,0,sizeof(response));
    request.op=op;request.sequence=++sequence;request.length=size;
    if(size)memcpy(request.payload,data,size);
    sm_seal(&request);
    unsigned long long started=diagnostic_enabled?sceKernelGetSystemTimeWide():0;
    int rc=sceIoDevctl("stm:",compact_packets?SM_DEV_EXCHANGE_COMPACT:SM_DEV_EXCHANGE,&request,sizeof(request),&response,sizeof(response));
    if(rc==SM_INVALID && compact_packets) {
        /* Older kernel bridge rejects the new ioctl before submitting USB. */
        compact_packets=0;
        rc=sceIoDevctl("stm:",SM_DEV_EXCHANGE,&request,sizeof(request),&response,sizeof(response));
    }
    unsigned duration=diagnostic_enabled?(unsigned)(sceKernelGetSystemTimeWide()-started):0;
    if(rc<0 && rc!=SM_BUSY){broken=1;wifi_state=SM_WIFI_FAILED;generation++;}
    if(rc>=0) {
        if(response.length>capacity || (response.length && !reply))rc=SM_INVALID;
        else {if(reply && response.length)memcpy(reply,response.payload,response.length);if(length)*length=response.length;rc=response.result;}
    }
    if(diagnostic_enabled) {
        if(!diagnostic.calls)diagnostic.start=entered;
        diagnostic.calls++;diagnostic.usb_us+=duration;diagnostic.wait_us+=started-entered;
        if(duration>diagnostic.max_us){diagnostic.max_us=duration;diagnostic.slow_op=op;}
        if(rc==SM_BUSY)diagnostic.busy++;else if(rc<0)diagnostic.errors++;
        if(op==SM_SOCKET_READ){diagnostic.reads++;if(rc>=0)diagnostic.bytes+=response.length;}
        if(op==SM_SOCKET_STATUS && rc>=0 && response.length==sizeof(SmSocketStatus)) {
            SmSocketStatus state;memcpy(&state,response.payload,sizeof(state));
            if(!diagnostic.samples || state.available<diagnostic.min)diagnostic.min=state.available;
            if(state.available>diagnostic.max)diagnostic.max=state.available;
            diagnostic.samples++;if(!state.available)diagnostic.empty++;
            if(state.available==65536)diagnostic.full++;
        }
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
        if(rc<0 || status<0){sceKernelUnloadModule(m);return rc<0?rc:status;}
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
        ahead_token=0;ahead_supported=1;
        generation++;broken=1;compact_packets=bulk_pairs=0;
        peer_caps=peer_caps_length=0;peer_caps_result=bridge_bulk_result=SM_OFFLINE;
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
    sceKernelSignalSema(rpc_lock,1);
    if(rc>=0) {
        unsigned caps=0,length=0;
        int probe=stm_rpc(SM_CAPABILITIES,NULL,0,&caps,sizeof(caps),&length,running);
        peer_caps=caps;peer_caps_length=length;peer_caps_result=probe;
        bridge_bulk_result=sceIoDevctl("stm:",SM_DEV_BULK_CAPS,NULL,0,NULL,0);
        if(probe==0 && length==sizeof(caps)) {
            compact_packets=(caps&SM_CAP_COMPACT)!=0;
            bulk_pairs=compact_packets && (caps&SM_CAP_BULK_PAIR) && bridge_bulk_result==1;
        }
        else if(probe!=SM_INVALID)rc=probe;
    }
    return rc;
}
void stm_driver_cancel(void) {if(module>=0)sceIoDevctl("stm:",SM_DEV_CANCEL,NULL,0,NULL,0);broken=1;generation++;}
void stm_driver_stop(void) {
    if(rpc_lock<0)return;
    stm_driver_cancel();
    if(lock(rpc_lock,1000,NULL)<0)return;
    ahead_token=0;
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
    if(domain!=AF_INET || type!=SOCK_STREAM || broken || lock(slots_lock,100,NULL)<0){socket_error(5);return -1;}
    int fd=-1;
    for(int i=0;i<LOCAL_SOCKETS;i++)if(!sockets[i].fd) {
        LocalSocket *s=&sockets[i];memset(s,0,sizeof(*s));
        s->fd=fd=FD_BASE+(next_fd++&0xfffffff);s->token=++next_token;if(!s->token)s->token=++next_token;
        s->epoch=generation;s->destination.token=s->token;s->destination.port=target_port;s->destination.tls=target_tls;
        snprintf(s->destination.host,sizeof(s->destination.host),"%s",target_host);break;
    }
    sceKernelSignalSema(slots_lock,1);socket_error(fd<0?24:0);return fd;
}
static int socket_open_pending(LocalSocket *s) {
    if(!s || s->dead || s->epoch!=generation || broken)return -1;
    if(s->opened)return 1;
    if(!s->opening)return -1;
    if(sceKernelGetSystemTimeWide()<s->open_retry)return 0;
    int rc=stm_rpc(SM_SOCKET_OPEN,&s->destination,sizeof(s->destination),NULL,0,NULL,NULL);
    if(rc==SM_BUSY){s->open_retry=sceKernelGetSystemTimeWide()+20000;return 0;}
    if(rc<0){s->dead=1;return -1;}
    s->opened=1;s->opening=0;return 1;
}
static int socket_status(LocalSocket *s,SmSocketStatus *status) {
    if(!s || !s->opened || s->dead || s->epoch!=generation || broken)return -1;
    SmSocketRequest r={s->token,0};unsigned length;
    int rc=stm_rpc(SM_SOCKET_STATUS,&r,sizeof(r),status,sizeof(*status),&length,NULL);
    if(rc==SM_BUSY)return 0;
    if(rc<0 || length!=sizeof(*status))return -1;
    if(diagnostic_enabled) {
        s->samples++;if(!status->available)s->empty_samples++;
        if(status->available==65536)s->full_samples++;
        if(status->available>s->max_available)s->max_available=status->available;
    }
    if(status->state==SM_SOCKET_READY)s->connected=1;
    return 1;
}
/* Called by the download owner before close. No extra USB transaction. */
int stm_download_snapshot(int fd,char *line,unsigned size) {
    if(!diagnostic_enabled || !virtual_fd(fd) || lock(rpc_lock,1,NULL)<0)return 0;
    LocalSocket *s=get(fd);int ok=s!=NULL;
    if(s)snprintf(line,size,"groups=%u bytes=%u short=%u finish_ms=%llu samples=%u empty=%u full=%u max_B=%u bulk=%d",
        s->groups,s->group_bytes,s->short_groups,s->finish_us/1000,s->samples,s->empty_samples,s->full_samples,s->max_available,bulk_pairs);
    sceKernelSignalSema(rpc_lock,1);return ok;
}
/* A read probe also collects the data. Previously POLLIN used a full STATUS
 * exchange followed by a second full READ exchange for the same 4 KiB block. */
static int socket_refill(LocalSocket *s) {
    if(s->end>s->pos || s->eof)return 1;
    if(sceKernelGetSystemTimeWide()<s->read_retry)return 0;
    if(diagnostic_enabled && sceKernelGetSystemTimeWide()>=s->diagnostic_next) {
        SmSocketStatus sample;
        s->diagnostic_next=sceKernelGetSystemTimeWide()+5000000;
        socket_status(s,&sample); /* One extra exchange per five seconds, not per block. */
    }
    SmSocketRequest r={s->token,SM_PAYLOAD_SIZE};unsigned length=0;
    int rc=0,cached=take_ahead(s,&rc,&length);
    if(cached<0)return 0;
    if(!cached)rc=stm_rpc(SM_SOCKET_READ,&r,sizeof(r),s->buffers[s->cache_index].payload,SM_PAIR_PAYLOAD_SIZE,&length,NULL);
    if(rc==SM_BUSY) {
        SceInt64 now=sceKernelGetSystemTimeWide();
        if(!s->empty_since)s->empty_since=now;
        unsigned limit=now-s->empty_since<250000?10000:100000;
        s->read_backoff=s->read_backoff?s->read_backoff*2:1000;
        if(s->read_backoff>limit)s->read_backoff=limit;
        s->read_retry=sceKernelGetSystemTimeWide()+s->read_backoff;return 0;
    }
    if(rc<0){s->dead=1;return -1;}
    s->read_backoff=0;s->read_retry=0;s->empty_since=0;s->pos=0;s->end=length;
    if(!length)s->eof=1;
    return 1;
}
int stm_connect(int fd,const struct sockaddr *address,socklen_t size) {
    if(!virtual_fd(fd))return sceNetInetConnect(fd,address,size);
    LocalSocket *s=get(fd);if(!s || s->dead || broken){socket_error(5);return -1;}
    s->opening=1;
    int rc=socket_open_pending(s);
    if(rc<0){socket_error(5);return -1;}
    if(s->nonblock){socket_error(119);return -1;}
    struct SceNetInetPollfd p={fd,SCE_NET_INET_POLLOUT,0};
    rc=stm_poll(&p,1,15000)>0 && (p.revents&SCE_NET_INET_POLLOUT)?0:-1;
    socket_error(rc<0?5:0);return rc;
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
            else {
                int rc=socket_open_pending(s);
                if(rc<0)p->revents=SCE_NET_INET_POLLERR;
                else if(rc>0) {
                    if(p->events&SCE_NET_INET_POLLIN) {
                        rc=socket_refill(s);
                        if(rc>0)p->revents|=SCE_NET_INET_POLLIN;
                        else if(rc<0)p->revents|=SCE_NET_INET_POLLERR;
                    }
                    if(p->events&SCE_NET_INET_POLLOUT) {
                        SmSocketStatus status={0};rc=socket_status(s,&status);
                        if(rc<0)p->revents|=SCE_NET_INET_POLLERR;
                        else if(rc>0) {
                            if(status.state==SM_SOCKET_READY && status.space>=SM_PAYLOAD_SIZE)p->revents|=SCE_NET_INET_POLLOUT;
                            if(status.state==SM_SOCKET_ERROR || status.state==SM_SOCKET_EOF)p->revents|=SCE_NET_INET_POLLERR;
                        }
                    }
                }
            }
            if(p->revents)ready++;
        }
        if(ready)return ready;
        if(sceKernelGetSystemTimeWide()>=end)break;
        sceKernelDelayThread(1000);
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
    LocalSocket *s=get(fd);
    *(int *)value=s && s->connected && !s->dead && !broken && s->epoch==generation?0:5;
    *size=sizeof(int);return 0;
}
size_t stm_send(int fd,const void *data,size_t size,int flags) {
    if(!virtual_fd(fd))return sceNetInetSend(fd,data,size,flags);
    LocalSocket *s=get(fd);if(!s || s->dead || broken || s->epoch!=generation){socket_error(5);return (size_t)-1;}
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
    socket_error(rc==SM_BUSY?35:rc<0?5:0);
    return rc<0?(size_t)-1:(size_t)rc;
}
size_t stm_recv(int fd,void *data,size_t size,int flags) {
    if(!virtual_fd(fd))return sceNetInetRecv(fd,data,size,flags);
    LocalSocket *s=get(fd);if(!s || s->dead || broken || s->epoch!=generation){socket_error(5);return (size_t)-1;}
    if(!size)return 0;
    if(s->pos==s->end) {
        int rc=socket_refill(s);
        if(rc<=0){socket_error(rc==0?35:5);return (size_t)-1;}
        if(s->eof)return 0;
    }
    if(size>s->end-s->pos)size=s->end-s->pos;
    memcpy(data,s->buffers[s->cache_index].payload+s->pos,size);s->pos+=size;
    if(s->pos==s->end && size>=1024)start_ahead(s);
    return size;
}
int stm_errno(void){
    if(!selected)return sceNetInetGetErrno();
    int tid=sceKernelGetThreadId();
    for(unsigned i=0;i<32;i++)if(thread_errors[i].tid==tid)return thread_errors[i].code;
    return 5;
}
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
    return n<0 && stm_errno()==35?-2:n;
}
int stm_tls_send(int fd,const void *data,int size,volatile int *running,int timeout) {
    if(!virtual_fd(fd))return tls_send(fd,data,size,running,timeout);
    int sent=0;SceInt64 end=sceKernelGetSystemTimeWide()+(SceInt64)timeout*1000;
    while(sent<size && (!running || *running) && sceKernelGetSystemTimeWide()<end) {
        struct SceNetInetPollfd p={fd,SCE_NET_INET_POLLOUT,0};int rc=stm_poll(&p,1,50);
        if(rc<0 || (p.revents&SCE_NET_INET_POLLERR))return -1;
        if(!rc)continue;
        int n=(int)stm_send(fd,(const char *)data+sent,size-sent,0);
        if(n<0 && stm_errno()==35)continue;
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
    if(lock(rpc_lock,1000,NULL)<0)return -1;
    finish_ahead();
    if(lock(slots_lock,1000,NULL)<0){sceKernelSignalSema(rpc_lock,1);return -1;}
    memset(s,0,sizeof(*s));sceKernelSignalSema(slots_lock,1);
    sceKernelSignalSema(rpc_lock,1);return 0;
}

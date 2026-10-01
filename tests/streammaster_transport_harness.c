/* Host seam for the actual ESP command handlers and PSP transport.
 * Hardware scheduling/TLS handshakes are not emulated. */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <unistd.h>
#include <sys/socket.h>
#include "streammaster/protocol.h"
static int64_t esp_timer_get_time(void){static int64_t ticks;return ++ticks;}
typedef int SceUID;
typedef unsigned SceUInt;
typedef int64_t SceInt64;
typedef int *SemaphoreHandle_t;
typedef void *TaskHandle_t;
typedef struct {int timeout_ms,is_plain_tcp;void *crt_bundle_attach;} esp_tls_cfg_t;
typedef struct {int fd;} esp_tls_t;
#define ESP_OK 0
#define ESP_TLS_ERR_SSL_WANT_READ -100
#define ESP_TLS_ERR_SSL_WANT_WRITE -101
#define MALLOC_CAP_8BIT 1
#define MALLOC_CAP_SPIRAM 2
#define CONFIG_BT_BLUEDROID_ENABLED 1
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY 0xffffffffU
static void *esp_crt_bundle_attach=(void *)1;
static int semaphores[32],semcount;
static SemaphoreHandle_t xSemaphoreCreateMutex(void){semaphores[++semcount]=1;return &semaphores[semcount];}
static int xSemaphoreTake(SemaphoreHandle_t s,unsigned wait){(void)wait;if(!*s)return 0;--*s;return 1;}
static void xSemaphoreGive(SemaphoreHandle_t s){++*s;}
static int xTaskCreate(void (*fn)(void *),const char *n,int size,void *p,int prio,TaskHandle_t *out){
    (void)fn;(void)n;(void)size;(void)p;(void)prio;*out=(void *)1;return 1;
}
static unsigned internal_staging,external_staging;
static void *heap_caps_malloc(size_t n,int flags){
    if(n==4096){if(flags&MALLOC_CAP_SPIRAM)external_staging+=n;else internal_staging+=n;}
    return malloc(n);
}
static jmp_buf owner_exit;
static int owner_notifications,owner_mode,owner_live,owner_reads,owner_writes,owner_delays;
static unsigned owner_write_size;
static unsigned char owner_write_copy[4096];
static void cancel_test_owner(void);
static void ulTaskNotifyTake(int clear,unsigned wait){
    (void)clear;(void)wait;if(owner_notifications++)longjmp(owner_exit,1);
}
static void xTaskNotifyGive(TaskHandle_t t){(void)t;}
static void vTaskDelay(int ticks){(void)ticks;assert(++owner_delays<20);}
static esp_tls_t *esp_tls_init(void){
    esp_tls_t *t=malloc(sizeof(*t));assert(t);t->fd=socket(AF_INET,SOCK_STREAM,0);assert(t->fd>=0);owner_live++;return t;
}
static int esp_tls_conn_new_sync(const char *host,int n,int port,const esp_tls_cfg_t *cfg,esp_tls_t *t){
    (void)host;(void)n;(void)port;(void)cfg;(void)t;return owner_mode==3?0:1;
}
static int esp_tls_get_conn_sockfd(esp_tls_t *t,int *fd){*fd=t->fd;return 0;}
static int esp_tls_conn_read(esp_tls_t *t,void *b,unsigned n){
    (void)t;owner_reads++;
    if(owner_mode==2){cancel_test_owner();return ESP_TLS_ERR_SSL_WANT_READ;}
    if(owner_reads==1){assert(n>=5);memcpy(b,"hello",5);return 5;}
    if(owner_mode==4)return -999;
    if(owner_reads==2)return ESP_TLS_ERR_SSL_WANT_READ;
    return 0;
}
static int esp_tls_conn_write(esp_tls_t *t,const void *b,unsigned n){
    (void)t;owner_writes++;
    if(owner_writes==1){owner_write_size=n;memcpy(owner_write_copy,b,n);return ESP_TLS_ERR_SSL_WANT_WRITE;}
    if(owner_writes==2){assert(n==owner_write_size && !memcmp(b,owner_write_copy,n));return n/2;}
    return n;
}
static void esp_tls_conn_destroy(esp_tls_t *t){
    if(owner_mode!=1){struct linger l={0};socklen_t n=sizeof(l);assert(!getsockopt(t->fd,SOL_SOCKET,SO_LINGER,&l,&n)&&l.l_onoff&&l.l_linger==0);}
    assert(!close(t->fd));free(t);owner_live--;
}
static unsigned sm_network_state(void){return SM_WIFI_READY;}
/* ACTUAL_ESP_SOCKETS */
static void cancel_test_owner(void){channels[0].cancel=1;}
static void exercise_owner(int mode){
    Channel *c=&channels[0];
    c->state=SM_SOCKET_CONNECTING;c->done=c->cancel=0;c->open.token=1;
    memset(&c->diag,0,sizeof(c->diag));c->diag.token=1;
    strcpy(c->open.host,"example.test");c->open.port=443;c->open.tls=1;
    c->rx_read=c->rx_write=c->tx_read=0;c->tx_write=16;memset(c->tx,42,16);
    owner_mode=mode;owner_notifications=owner_reads=owner_writes=owner_delays=0;
    if(!setjmp(owner_exit))socket_worker(c);
    assert(c->done && !owner_live);
    if(mode==1){assert(c->state==SM_SOCKET_EOF && c->tx_read==16 && c->rx_write==5 && owner_writes==3);
        assert(c->diag.rx_bytes==5 && c->diag.tx_bytes==16 && c->diag.again==1 && c->diag.loops==3 && c->diag.last_io==0);}
    else if(mode==2)assert(c->state==SM_SOCKET_FREE);
    else assert(c->state==SM_SOCKET_ERROR);
    c->state=SM_SOCKET_FREE;c->cancel=0;
}

#define SO_NONBLOCK 0x1009
#define PSP_NET_APCTL_STATE_GOT_IP 4
#define SCE_NET_INET_POLLIN 1
#define SCE_NET_INET_POLLOUT 4
#define SCE_NET_INET_POLLERR 8
struct SceNetInetPollfd {int fd;short events,revents;};
static int64_t clock_us;
static unsigned rpc_count,held_token;
static int injected_error,old_firmware;
static int test_tid=1;
static int sceKernelGetThreadId(void){return test_tid;}
static SceInt64 sceKernelGetSystemTimeWide(void){return clock_us;}
static void sceKernelDelayThread(int us){clock_us+=us;}
static int sceKernelCreateSema(const char *n,int attr,int initial,int max,void *v){
    (void)n;(void)attr;(void)max;(void)v;semaphores[++semcount]=initial;return semcount;
}
static int sceKernelWaitSema(int id,int n,unsigned *wait){
    (void)n;if(semaphores[id]){--semaphores[id];return 0;}clock_us+=*wait;return -1;
}
static int sceKernelSignalSema(int id,int n){semaphores[id]+=n;return 0;}
static int kuKernelLoadModule(const char *path,int flags,void *v){(void)flags;(void)v;return strstr(path,"flash0:")?(int)0x80020139:10;}
static int sceKernelStartModule(int m,int n,void *a,int *s,void *v){(void)m;(void)n;(void)a;(void)v;*s=0;return 0;}
static int sceKernelStopModule(int m,int n,void *a,int *s,void *v){return sceKernelStartModule(m,n,a,s,v);}
static int sceKernelUnloadModule(int m){(void)m;return 0;}
static int legacy_caps,legacy_bridge,legacy_async;
static int bulk_firmware=1,bulk_bridge=1;
static int extended_firmware,extended_bridge;
static SmFrame pending_reply;
static SmBulkResult pending_bulk;
static int async_pending,async_begins,async_finishes;
static int sceIoDevctl(const char *name,unsigned op,void *in,int inlen,void *out,int outlen){
    (void)name;(void)inlen;(void)outlen;
    if(op==SM_DEV_STATUS)return 1;
    if(op==SM_DEV_BULK_CAPS)return bulk_bridge && !legacy_bridge && !legacy_async?1:SM_INVALID;
    if(op==SM_DEV_BULK_EXT_CAPS)return extended_bridge?1:SM_INVALID;
    if(op==SM_DEV_BULK_BEGIN) {
        assert(bulk_firmware && bulk_bridge && !async_pending);
        SmFrame r=*(SmFrame *)in;SmBulkFrame b;
        pending_bulk.length=0;pending_bulk.result=0;
        int depth=r.op==SM_SOCKET_READ_BULK_EXT?r.result:2;
        for(int i=0;i<depth;i++) {
            sm_sockets_bulk_read(&r,&b);assert(sm_bulk_valid(&b));r.sequence++;
            memcpy(pending_bulk.payload+pending_bulk.length,b.payload,b.length);pending_bulk.length+=b.length;
            if(b.result<0)pending_bulk.result=b.result;
        }
        if(pending_bulk.length)pending_bulk.result=0;
        async_pending=1;async_begins++;return 0;
    }
    if(op==SM_DEV_BULK_FINISH) {
        assert(async_pending);async_pending=0;async_finishes++;
        assert(outlen==SM_LEGACY_RESULT_SIZE || outlen==sizeof(pending_bulk));
        memcpy(out,&pending_bulk,8+pending_bulk.length);return 0;
    }
    if(op==SM_DEV_STOP || op==SM_DEV_CANCEL){async_pending=0;return 0;}
    if(op==SM_DEV_READ_BEGIN) {
        if(legacy_bridge || legacy_async)return SM_INVALID;
        assert(!async_pending);async_begins++;
        int rc=sceIoDevctl(name,SM_DEV_EXCHANGE_COMPACT,in,inlen,&pending_reply,sizeof(pending_reply));
        if(rc>=0)async_pending=1;
        return rc;
    }
    if(op==SM_DEV_READ_FINISH) {
        assert(async_pending);async_pending=0;async_finishes++;
        memcpy(out,&pending_reply,sizeof(pending_reply));return 0;
    }
    if(op!=SM_DEV_EXCHANGE && op!=SM_DEV_EXCHANGE_COMPACT)return 0;
    if(op==SM_DEV_EXCHANGE_COMPACT && legacy_bridge)return SM_INVALID;
    rpc_count++;
    if(injected_error)return injected_error;
    SmFrame *r=in,*reply=out;assert(sm_valid(r));
    memset(reply,0,sizeof(*reply));reply->op=r->op;reply->sequence=r->sequence;reply->flags=SM_REPLY;
    /* Simulate independent connection completion and owner-only cleanup. */
    for(int i=0;i<SM_SOCKET_COUNT;i++) {
        Channel *c=&channels[i];
        if(c->cancel){c->state=SM_SOCKET_FREE;c->done=1;c->cancel=0;}
        else if(c->state==SM_SOCKET_CONNECTING && c->open.token!=held_token)c->state=SM_SOCKET_READY;
    }
    if(r->op==SM_INFO){SmInfo info={.wifi_state=SM_WIFI_READY};memcpy(reply->payload,&info,sizeof(info));reply->length=sizeof(info);}
    else if(r->op==SM_CONNECT)reply->result=0;
    else if(old_firmware)reply->result=SM_INVALID;
    else if(r->op==SM_CAPABILITIES){if(legacy_caps)reply->result=SM_INVALID;else {unsigned caps=SM_CAP_COMPACT|(bulk_firmware?SM_CAP_BULK_PAIR:0)|(extended_firmware?SM_CAP_BULK_EXT:0);memcpy(reply->payload,&caps,sizeof(caps));reply->length=sizeof(caps);}}
    else sm_sockets_command(r,reply);
    sm_seal(reply);return 0;
}
static int sceNetInetSocket(int a,int b,int c){(void)a;(void)b;(void)c;return 7;}
static int sceNetInetConnect(int fd,const struct sockaddr *a,socklen_t n){(void)fd;(void)a;(void)n;return 0;}
static int sceNetInetPoll(struct SceNetInetPollfd *p,size_t n,int t){(void)p;(void)n;(void)t;return 0;}
static int sceNetInetSetsockopt(int f,int l,int o,const void *v,socklen_t n){(void)f;(void)l;(void)o;(void)v;(void)n;return 0;}
static int sceNetInetGetsockopt(int f,int l,int o,void *v,socklen_t *n){(void)f;(void)l;(void)o;(void)v;(void)n;return 0;}
static size_t sceNetInetSend(int f,const void *b,size_t n,int flags){(void)f;(void)b;(void)flags;return n;}
static size_t sceNetInetRecv(int f,void *b,size_t n,int flags){(void)f;(void)b;(void)n;(void)flags;return 0;}
static int sceNetInetGetErrno(void){return 123;}
static int sceNetApctlGetState(int *s){*s=4;return 0;}
static int tls_open(int f,const char *h,int p,volatile int *r,int t){(void)f;(void)h;(void)p;(void)r;(void)t;return 0;}
static int tls_recv(int f,void *b,int n,int t){(void)f;(void)b;(void)n;(void)t;return 0;}
static int tls_send(int f,const void *b,int n,volatile int *r,int t){(void)f;(void)b;(void)r;(void)t;return n;}
static int tls_close(int f){(void)f;return 0;}
int stm_poll(struct SceNetInetPollfd *,size_t,int);
/* ACTUAL_PSP_TRANSPORT */

static Channel *channel(int fd){
    LocalSocket *s=get(fd);assert(s);
    for(int i=0;i<SM_SOCKET_COUNT;i++)if(channels[i].open.token==s->token)return &channels[i];
    abort();
}
static int open_socket(void){
    int fd=stm_socket(AF_INET,SOCK_STREAM,0);assert(fd>=FD_BASE);
    assert(stm_connect(fd,NULL,0)==0);return fd;
}
int main(void){
    sm_sockets_init();
    assert(internal_staging==16384 && external_staging==32768);
    for(int cycle=0;cycle<1000;cycle++)for(int mode=1;mode<=4;mode++)exercise_owner(mode);
    assert(!owner_live);
    assert(stm_init(0,"example.test",443,1)==0);
    assert(stm_socket(AF_INET,SOCK_STREAM,0)==7 && stm_errno()==123);
    assert(stm_init(1,"example.test",443,1)==0);
    volatile int running=1;old_firmware=1;
    assert(stm_associate(&running,0)==SM_INVALID);
    assert(stm_associate(&running,0)==SM_INVALID); /* No false success on retry. */
    old_firmware=0;assert(stm_associate(&running,0)==0);
    int fds[6];for(int i=0;i<6;i++)fds[i]=open_socket();
    int seventh=stm_socket(AF_INET,SOCK_STREAM,0);
    assert(stm_connect(seventh,NULL,0)<0);assert(stm_close(seventh)==0);
    for(int i=0;i<6;i++) {
        Channel *c=channel(fds[i]);assert(c->open.tls && c->open.port==443);
        assert(!strcmp(c->open.host,"example.test"));
        unsigned char data[40],got[40];memset(data,20+i,sizeof(data));
        /* Wrapped TX and RX, also across UINT_MAX. */
        c->tx_read=c->tx_write=UINT32_MAX-15;
        assert(stm_send(fds[i],data,sizeof(data),0)==sizeof(data));
        copy_out(got,c->tx,TX_SIZE,c->tx_read,sizeof(got));assert(!memcmp(got,data,sizeof(got)));
        c->rx_read=UINT32_MAX-15;c->rx_write=c->rx_read+sizeof(data);
        copy_in(c->rx,RX_SIZE,c->rx_read,data,sizeof(data));
        unsigned before=rpc_count;
        for(int j=0;j<40;j++)assert(stm_recv(fds[i],got+j,1,0)==1);
        assert(rpc_count==before+1 && !memcmp(got,data,sizeof(got)));
        assert((int)stm_recv(fds[i],got,1,0)==-1 && stm_errno()==35);
        assert(get(fds[i])->read_backoff==1000);
        for(int retry=0;retry<8;retry++) {
            clock_us+=10000;
            assert((int)stm_recv(fds[i],got,1,0)==-1 && stm_errno()==35);
            assert(get(fds[i])->read_backoff<=10000);
        }
    }
    /* One slow connection must not prevent another from reading. */
    clock_us+=100000; /* Let the previous empty-read backoff expire. */
    Channel *c=channel(fds[0]);c->state=SM_SOCKET_CONNECTING;held_token=c->open.token;
    c=channel(fds[1]);copy_in(c->rx,RX_SIZE,c->rx_write,(const unsigned char *)"last",4);c->rx_write+=4;c->state=SM_SOCKET_ERROR;
    struct SceNetInetPollfd p[2]={{fds[0],SCE_NET_INET_POLLOUT,0},{fds[1],SCE_NET_INET_POLLIN,0}};
    assert(stm_poll(p,2,0)==1 && !p[0].revents && (p[1].revents&SCE_NET_INET_POLLIN));
    char got[8];assert(stm_recv(fds[1],got,8,0)==4 && !memcmp(got,"last",4));
    assert(stm_poll(p+1,1,0)==1 && (p[1].revents&SCE_NET_INET_POLLERR));
    /* Waiting POLLOUT cannot flood USB, even with repeated nonblocking polls.
     * Ready sockets are never cached and a second socket remains independent. */
    unsigned status_before=rpc_count;
    for(int i=0;i<100;i++)assert(stm_poll(p,1,0)==0);
    assert(rpc_count==status_before);
    assert(stm_poll(p,1,1000)==0);
    assert(rpc_count-status_before>=49 && rpc_count-status_before<=51);
    c=channel(fds[0]);c->state=SM_SOCKET_READY;held_token=0;clock_us+=20000;
    assert(stm_poll(p,1,0)==1 && (p[0].revents&SCE_NET_INET_POLLOUT));
    c->state=SM_SOCKET_ERROR;
    assert(stm_poll(p,1,0)==1 && (p[0].revents&SCE_NET_INET_POLLERR));
    SmNetDiag net={0};sm_sockets_diagnostic(&net);
    assert(net.sampled==63 && net.socket[0].token==c->diag.token && net.socket[0].state==SM_SOCKET_ERROR);
    *channels[0].lock=0;memset(&net,0,sizeof(net));sm_sockets_diagnostic(&net);
    assert(net.sampled==62);*channels[0].lock=1; /* Diagnostics never wait on an owner. */
    /* Buffered bytes cannot survive transport cancellation/re-enumeration. */
    clock_us+=100000;
    c=channel(fds[2]);copy_in(c->rx,RX_SIZE,c->rx_write,(const unsigned char *)"abc",3);c->rx_write+=3;
    assert(stm_recv(fds[2],got,1,0)==1);
    stm_driver_cancel();p[0]=(struct SceNetInetPollfd){fds[2],SCE_NET_INET_POLLIN,0};
    assert(stm_poll(p,1,0)==1 && p[0].revents==SCE_NET_INET_POLLERR);
    for(int i=0;i<6;i++)assert(stm_close(fds[i])==0);
    held_token=0;assert(stm_associate(&running,1)==0);
    int fd=open_socket();c=channel(fd);c->state=SM_SOCKET_EOF;
    assert(stm_recv(fd,got,1,0)==0);assert(stm_close(fd)==0);
    fd=open_socket();injected_error=SM_TIMEOUT;
    assert(stm_poll(&(struct SceNetInetPollfd){fd,SCE_NET_INET_POLLIN,0},1,0)==1 && broken);
    assert(stm_close(fd)==0);injected_error=0;
    assert(stm_associate(&running,1)==0);
    /* Poll now fetches data: one exchange, not STATUS plus READ. */
    fd=open_socket();c=channel(fd);
    copy_in(c->rx,RX_SIZE,c->rx_write,(const unsigned char *)"xyz",3);c->rx_write+=3;
    unsigned before=rpc_count;
    p[0]=(struct SceNetInetPollfd){fd,SCE_NET_INET_POLLIN,0};
    assert(stm_poll(p,1,0)==1 && stm_recv(fd,got,3,0)==3 && rpc_count==before+1);
    before=rpc_count;assert(stm_poll(p,1,1000)==0);
    assert(rpc_count-before<45); /* Fast probes only for the first 250 ms, then idle backoff. */
    assert(stm_close(fd)==0);
    /* Pipelined block B is consumed on USB while the caller processes A.
     * A control RPC drains B into its owner's mailbox, never its own reply. */
    fd=open_socket();c=channel(fd);
    unsigned char bulk[SM_PAYLOAD_SIZE*2],copy[SM_PAYLOAD_SIZE];
    for(unsigned i=0;i<sizeof(bulk);i++)bulk[i]=(i/SM_PAYLOAD_SIZE)*37+i%251;
    copy_in(c->rx,RX_SIZE,c->rx_write,bulk,sizeof(bulk));c->rx_write+=sizeof(bulk);
    assert(stm_recv(fd,copy,sizeof(copy),0)==sizeof(copy) && !memcmp(copy,bulk,sizeof(copy)));
    assert(async_pending && async_begins);
    SmInfo info;unsigned length;
    assert(stm_rpc(SM_INFO,NULL,0,&info,sizeof(info),&length,NULL)==0 && length==sizeof(info));
    assert(!async_pending && async_finishes);
    assert(stm_recv(fd,copy,sizeof(copy),0)==sizeof(copy) && !memcmp(copy,bulk+sizeof(copy),sizeof(copy)));
    assert(stm_close(fd)==0 && !async_pending);
    /* A detach with outstanding DMA must not deliver data after reassociation. */
    fd=open_socket();c=channel(fd);
    copy_in(c->rx,RX_SIZE,c->rx_write,bulk,sizeof(bulk));c->rx_write+=sizeof(bulk);
    assert(stm_recv(fd,copy,sizeof(copy),0)==sizeof(copy) && async_pending);
    stm_driver_cancel();assert(!async_pending);
    assert(stm_associate(&running,1)==0);
    assert((int)stm_recv(fd,copy,sizeof(copy),0)<0);
    assert(stm_close(fd)==0);
    /* Compact-capable older bridges lack the asynchronous local ioctl. */
    legacy_async=1;assert(stm_associate(&running,1)==0 && !bulk_pairs);fd=open_socket();c=channel(fd);
    copy_in(c->rx,RX_SIZE,c->rx_write,bulk,sizeof(bulk));c->rx_write+=sizeof(bulk);
    assert(stm_recv(fd,copy,sizeof(copy),0)==sizeof(copy) && !ahead_supported);
    assert(stm_recv(fd,copy,sizeof(copy),0)==sizeof(copy) && !memcmp(copy,bulk+sizeof(copy),sizeof(copy)));
    assert(stm_close(fd)==0);legacy_async=0;
    assert(stm_associate(&running,1)==0 && bulk_pairs);
    fd=open_socket();c=channel(fd);
    unsigned char large[SM_PAIR_PAYLOAD_SIZE*3],chunk[SM_PAIR_PAYLOAD_SIZE];
    for(unsigned i=0;i<sizeof(large);i++)large[i]=(i*7+i/257)%251;
    c->rx_read=c->rx_write=RX_SIZE-100;
    copy_in(c->rx,RX_SIZE,c->rx_write,large,sizeof(large));c->rx_write+=sizeof(large);c->state=SM_SOCKET_EOF;
    unsigned consumed=0,max_read=0;
    while(consumed<sizeof(large)) {
        int n=(int)stm_recv(fd,chunk,sizeof(chunk),0);assert(n>0);
        assert(!memcmp(chunk,large+consumed,n));consumed+=n;
        if((unsigned)n>max_read)max_read=n;
    }
    assert(max_read==SM_PAIR_PAYLOAD_SIZE && stm_recv(fd,chunk,sizeof(chunk),0)==0);
    assert(stm_close(fd)==0);
    bulk_firmware=0;assert(stm_associate(&running,1)==0 && !bulk_pairs && compact_packets);
    bulk_firmware=1;
    /* Repeated short-lived remote owners cannot exhaust either fixed pool. */
    stm_diagnostic_enable(1);
    fd=open_socket();c=channel(fd);
    copy_in(c->rx,RX_SIZE,c->rx_write,(const unsigned char *)"diag",4);c->rx_write+=4;
    before=rpc_count;
    assert(stm_recv(fd,got,4,0)==4 && rpc_count==before+2);
    assert(diagnostic.bytes==4 && diagnostic.reads==1 && diagnostic.max==4);
    char report[176];
    assert(stm_diagnostic_snapshot(report,sizeof(report),0)&&strstr(report,"reads=1"));
    assert(stm_diagnostic_snapshot(report,sizeof(report),1)&&strstr(report,"max_B=4"));
    before=rpc_count;
    assert(stm_download_snapshot(fd,report,sizeof(report))&&strstr(report,"max_B=4"));
    assert(rpc_count==before); /* Reporting must not issue another USB request. */
    stm_diagnostic_enable(0);
    assert(!stm_download_snapshot(fd,report,sizeof(report)));
    assert(!stm_diagnostic_snapshot(report,sizeof(report),0));
    assert(stm_close(fd)==0);
    for(int cycle=0;cycle<10000;cycle++) {
        test_tid=cycle+2;fd=open_socket();c=channel(fd);c->state=SM_SOCKET_EOF;c->done=1;
        assert(stm_recv(fd,got,1,0)==0);assert(stm_close(fd)==0);
        socket_error(35);assert(stm_errno()==35);stm_thread_finished();
    }
    test_tid=1;socket_error(35);test_tid=2;socket_error(5);
    assert(stm_errno()==5);test_tid=1;assert(stm_errno()==35);stm_thread_finished();
    test_tid=2;stm_thread_finished();
    for(int i=0;i<32;i++)assert(!thread_errors[i].tid);
    for(int i=0;i<LOCAL_SOCKETS;i++)assert(!sockets[i].fd);
    for(int i=0;i<SM_SOCKET_COUNT;i++)assert(channels[i].state==SM_SOCKET_FREE);
    legacy_caps=1;assert(stm_associate(&running,1)==0 && !compact_packets);
    legacy_caps=0;assert(stm_associate(&running,1)==0 && compact_packets);
    legacy_bridge=1;assert(stm_associate(&running,1)==0 && !compact_packets);
    legacy_bridge=0;
    running=0;clock_us=0;semaphores[rpc_lock]=0;
    assert(stm_rpc(SM_INFO,NULL,0,NULL,0,NULL,&running)==SM_BUSY && clock_us==0);
    semaphores[rpc_lock]=1;stm_driver_stop();
    running=1;
    for(unsigned kib=8;kib<=32;kib*=2)for(unsigned depth=1;depth<=4;depth*=2) {
        stm_tuning(kib,depth);extended_firmware=extended_bridge=1;
        assert(!stm_associate(&running,1));
        unsigned expected_depth=kib==32&&depth==4?2:depth;
        assert(bulk_depth==expected_depth && bulk_payload==kib*1024-32);
        fd=open_socket();c=channel(fd);
        unsigned char input[65536],output[65536];
        for(unsigned i=0;i<sizeof(input);i++)input[i]=(i*13+i/255)%251;
        c->rx_read=c->rx_write=RX_SIZE-17;copy_in(c->rx,RX_SIZE,c->rx_write,input,sizeof(input));
        c->rx_write+=sizeof(input);c->state=SM_SOCKET_EOF;consumed=0;
        while(consumed<sizeof(input)) {
            int n=stm_recv(fd,output,sizeof(output),0);assert(n>0 && (unsigned)n<=sizeof(input)-consumed);
            assert(!memcmp(output,input+consumed,n));consumed+=n;
        }
        assert(!stm_recv(fd,output,sizeof(output),0));assert(!stm_close(fd));
    }
    stm_tuning(0,0);extended_bridge=extended_firmware=1;
    assert(!stm_associate(&running,1) && bulk_extended && bulk_depth==4 && bulk_payload==8160);
    stm_tuning(8,2);assert(!stm_associate(&running,1) && !bulk_extended && bulk_depth==2);
    stm_tuning(0,0);
    extended_bridge=0;assert(!stm_associate(&running,1) && !bulk_extended && bulk_depth==2 && bulk_payload==8160);
    extended_bridge=1;extended_firmware=0;assert(!stm_associate(&running,1) && !bulk_extended);
    stm_driver_stop();
    for(int i=0;i<SM_SOCKET_COUNT;i++){free(channels[i].rx);free(channels[i].tx);free(channels[i].block);free(channels[i].write_block);}
    puts("StreamMaster: legacy/extended profiles, 10000 PSP cycles + 4000 ESP owner runs; isolation, cleanup and fallback OK");
}

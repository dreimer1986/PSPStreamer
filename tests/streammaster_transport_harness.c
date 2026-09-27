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
#include <sys/socket.h>
#include "streammaster/protocol.h"
typedef int SceUID;
typedef unsigned SceUInt;
typedef int64_t SceInt64;
typedef int *SemaphoreHandle_t;
typedef void *TaskHandle_t;
typedef struct {int timeout_ms,is_plain_tcp;void *crt_bundle_attach;} esp_tls_cfg_t;
typedef struct {int unused;} esp_tls_t;
#define ESP_OK 0
#define ESP_TLS_ERR_SSL_WANT_READ -100
#define ESP_TLS_ERR_SSL_WANT_WRITE -101
#define MALLOC_CAP_8BIT 1
#define MALLOC_CAP_SPIRAM 2
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
static void *heap_caps_malloc(size_t n,int flags){(void)flags;return malloc(n);}
static void ulTaskNotifyTake(int clear,unsigned wait){(void)clear;(void)wait;}
static void xTaskNotifyGive(TaskHandle_t t){(void)t;}
static void vTaskDelay(int ticks){(void)ticks;}
static esp_tls_t *esp_tls_init(void){return NULL;}
static int esp_tls_conn_new_sync(const char *host,int n,int port,const esp_tls_cfg_t *cfg,esp_tls_t *t){
    (void)host;(void)n;(void)port;(void)cfg;(void)t;return 0;
}
static int esp_tls_get_conn_sockfd(esp_tls_t *t,int *fd){(void)t;*fd=-1;return -1;}
static int esp_tls_conn_read(esp_tls_t *t,void *b,unsigned n){(void)t;(void)b;(void)n;return -1;}
static int esp_tls_conn_write(esp_tls_t *t,const void *b,unsigned n){(void)t;(void)b;(void)n;return -1;}
static void esp_tls_conn_destroy(esp_tls_t *t){(void)t;}
static unsigned sm_network_state(void){return SM_WIFI_READY;}
/* ACTUAL_ESP_SOCKETS */

#define SO_NONBLOCK 0x1009
#define PSP_NET_APCTL_STATE_GOT_IP 4
#define SCE_NET_INET_POLLIN 1
#define SCE_NET_INET_POLLOUT 4
#define SCE_NET_INET_POLLERR 8
struct SceNetInetPollfd {int fd;short events,revents;};
static int64_t clock_us;
static unsigned rpc_count,held_token;
static int injected_error,old_firmware;
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
static int sceIoDevctl(const char *name,unsigned op,void *in,int inlen,void *out,int outlen){
    (void)name;(void)inlen;(void)outlen;
    if(op==SM_DEV_STATUS)return 1;
    if(op!=SM_DEV_EXCHANGE)return 0;
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
    sm_sockets_init();assert(stm_init(0,"example.test",443,1)==0);
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
    }
    /* One slow connection must not prevent another from reading. */
    Channel *c=channel(fds[0]);c->state=SM_SOCKET_CONNECTING;held_token=c->open.token;
    c=channel(fds[1]);copy_in(c->rx,RX_SIZE,c->rx_write,(const unsigned char *)"last",4);c->rx_write+=4;c->state=SM_SOCKET_ERROR;
    struct SceNetInetPollfd p[2]={{fds[0],SCE_NET_INET_POLLOUT,0},{fds[1],SCE_NET_INET_POLLIN,0}};
    assert(stm_poll(p,2,0)==1 && !p[0].revents && (p[1].revents&SCE_NET_INET_POLLIN));
    char got[8];assert(stm_recv(fds[1],got,8,0)==4 && !memcmp(got,"last",4));
    assert(stm_poll(p+1,1,0)==1 && (p[1].revents&SCE_NET_INET_POLLERR));
    /* Buffered bytes cannot survive transport cancellation/re-enumeration. */
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
    running=0;clock_us=0;semaphores[rpc_lock]=0;
    assert(stm_rpc(SM_INFO,NULL,0,NULL,0,NULL,&running)==SM_BUSY && clock_us==0);
    semaphores[rpc_lock]=1;stm_driver_stop();
    for(int i=0;i<SM_SOCKET_COUNT;i++){free(channels[i].rx);free(channels[i].tx);free(channels[i].block);free(channels[i].write_block);}
    puts("StreamMaster: six isolated channels, wrap/cache/EOF, cancellation, retry and native fallback OK");
}

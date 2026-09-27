/* SPDX-License-Identifier: GPL-2.0-or-later
 * Independent owners for TCP/TLS. No DNS, connect, read or TLS handshake in
 * the USB command task. Slots are never recycled while their owner is alive. */
#include "bridge.h"
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "lwip/sockets.h"
#define RX_SIZE 65536U
#define TX_SIZE 8192U
typedef struct {
    SemaphoreHandle_t lock;
    TaskHandle_t worker;
    SmSocketOpen open;
    unsigned state,rx_read,rx_write,tx_read,tx_write;
    int error,done,cancel;
    unsigned char *rx,*tx,*block,*write_block;
} Channel;
static Channel channels[SM_SOCKET_COUNT];
static atomic_bool reset_pending;
static void copy_out(unsigned char *dst,const unsigned char *ring,unsigned cap,unsigned pos,unsigned n) {
    unsigned first=cap-pos%cap;if(first>n)first=n;
    memcpy(dst,ring+pos%cap,first);memcpy(dst+first,ring,n-first);
}
static void copy_in(unsigned char *ring,unsigned cap,unsigned pos,const unsigned char *src,unsigned n) {
    unsigned first=cap-pos%cap;if(first>n)first=n;
    memcpy(ring+pos%cap,src,first);memcpy(ring,src+first,n-first);
}
static int would_block(int n) {
    return n==ESP_TLS_ERR_SSL_WANT_READ || n==ESP_TLS_ERR_SSL_WANT_WRITE ||
        (n<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR));
}
static void socket_worker(void *arg) {
    Channel *c=arg;
    unsigned char *block=c->block;
    for(;;) {
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        xSemaphoreTake(c->lock,portMAX_DELAY);
        if(c->state!=SM_SOCKET_CONNECTING){xSemaphoreGive(c->lock);continue;}
        SmSocketOpen open=c->open;int cancelled=c->cancel;xSemaphoreGive(c->lock);
        esp_tls_t *tls=cancelled?NULL:esp_tls_init();
        esp_tls_cfg_t cfg={.timeout_ms=5000,.is_plain_tcp=!open.tls,
            .crt_bundle_attach=open.tls?esp_crt_bundle_attach:NULL};
        int ok=tls && esp_tls_conn_new_sync(open.host,strlen(open.host),open.port,&cfg,tls)==1;
        int fd=-1;
        if(ok)ok=esp_tls_get_conn_sockfd(tls,&fd)==ESP_OK &&
            fcntl(fd,F_SETFL,fcntl(fd,F_GETFL,0)|O_NONBLOCK)>=0;
        xSemaphoreTake(c->lock,portMAX_DELAY);
        c->state=ok?SM_SOCKET_READY:SM_SOCKET_ERROR;c->error=ok?0:SM_IO;
        xSemaphoreGive(c->lock);
        unsigned pending=0;
        while(ok) {
            for(int burst=0;burst<8;burst++) {
                int progress=0;
                xSemaphoreTake(c->lock,portMAX_DELAY);
                cancelled=c->cancel;
                if(!pending) {
                    pending=c->tx_write-c->tx_read;
                    if(pending>4096)pending=4096;
                    if(pending)copy_out(c->write_block,c->tx,TX_SIZE,c->tx_read,pending);
                }
                xSemaphoreGive(c->lock);
                if(cancelled){ok=0;break;}
                if(pending) {
                    errno=0;int n=esp_tls_conn_write(tls,c->write_block,pending);
                    if(n>0){xSemaphoreTake(c->lock,portMAX_DELAY);c->tx_read+=n;xSemaphoreGive(c->lock);pending=0;progress=1;}
                    else if(!would_block(n)){ok=0;break;}
                }
                xSemaphoreTake(c->lock,portMAX_DELAY);
                unsigned space=RX_SIZE-(c->rx_write-c->rx_read);xSemaphoreGive(c->lock);
                if(space) {
                    if(space>4096)space=4096;
                    errno=0;int n=esp_tls_conn_read(tls,block,space);
                    if(n>0) {
                        xSemaphoreTake(c->lock,portMAX_DELAY);copy_in(c->rx,RX_SIZE,c->rx_write,block,n);c->rx_write+=n;
                        xSemaphoreGive(c->lock);progress=1;
                    } else if(!n) {
                        xSemaphoreTake(c->lock,portMAX_DELAY);c->state=SM_SOCKET_EOF;xSemaphoreGive(c->lock);ok=0;break;
                    } else if(!would_block(n)){ok=0;break;}
                }
                if(!progress)break;
            }
            vTaskDelay(1); /* Explicitly yield even under continuous traffic. */
        }
        if(tls) {
            xSemaphoreTake(c->lock,portMAX_DELAY);
            int abort_connection=c->cancel || c->state!=SM_SOCKET_EOF;
            xSemaphoreGive(c->lock);
            /* Explicit cancellation must not retain TCP send queues/PCBs while
             * a vanished peer never acknowledges them. Only the owner closes. */
            int closing_fd=-1;
            if(abort_connection && esp_tls_get_conn_sockfd(tls,&closing_fd)==ESP_OK && closing_fd>=0) {
                struct linger immediate={1,0};
                setsockopt(closing_fd,SOL_SOCKET,SO_LINGER,&immediate,sizeof(immediate));
            }
            esp_tls_conn_destroy(tls);
        }
        memset(block,0,4096);
        memset(c->write_block,0,4096);
        xSemaphoreTake(c->lock,portMAX_DELAY);
        c->done=1;
        if(c->cancel){c->state=SM_SOCKET_FREE;memset(&c->open,0,sizeof(c->open));}
        else if(c->state!=SM_SOCKET_EOF){c->state=SM_SOCKET_ERROR;c->error=SM_IO;}
        xSemaphoreGive(c->lock);
    }
}
void sm_sockets_init(void) {
    for(int i=0;i<SM_SOCKET_COUNT;i++) {
        Channel *c=&channels[i];c->done=1;
        c->lock=xSemaphoreCreateMutex();
        c->rx=heap_caps_malloc(RX_SIZE,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        c->tx=heap_caps_malloc(TX_SIZE,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        c->block=heap_caps_malloc(4096,MALLOC_CAP_8BIT);
        c->write_block=heap_caps_malloc(4096,MALLOC_CAP_8BIT);
        if(!c->lock || !c->rx || !c->tx || !c->block || !c->write_block || xTaskCreate(socket_worker,"socket",8192,c,5,&c->worker)!=pdPASS)abort();
    }
}
void sm_sockets_reset(void){atomic_store(&reset_pending,true);}
void sm_sockets_idle(void) {
    if(!atomic_exchange(&reset_pending,false))return;
    for(int i=0;i<SM_SOCKET_COUNT;i++) {
        Channel *c=&channels[i];xSemaphoreTake(c->lock,portMAX_DELAY);
        if(c->state!=SM_SOCKET_FREE) {
            c->cancel=1;if(c->done){c->state=SM_SOCKET_FREE;memset(&c->open,0,sizeof(c->open));}
        }
        xSemaphoreGive(c->lock);
    }
}
void sm_sockets_command(const SmFrame *r,SmFrame *out) {
    sm_sockets_idle();
    if(r->op==SM_SOCKET_RESET){sm_sockets_reset();sm_sockets_idle();return;}
    if(r->op==SM_SOCKET_OPEN) {
        if(r->length!=sizeof(SmSocketOpen)){out->result=SM_INVALID;return;}
        SmSocketOpen open;memcpy(&open,r->payload,sizeof(open));
        if(!open.token || !open.port || open.port>65535 || open.tls>1 ||
           !memchr(open.host,0,sizeof(open.host)) || !open.host[0]){out->result=SM_INVALID;return;}
        if(sm_network_state()!=SM_WIFI_READY){out->result=SM_OFFLINE;return;}
        for(int i=0;i<SM_SOCKET_COUNT;i++) {
            Channel *c=&channels[i];
            if(xSemaphoreTake(c->lock,0)!=pdTRUE)continue;
            if(c->state==SM_SOCKET_FREE && c->done) {
                c->open=open;c->state=SM_SOCKET_CONNECTING;c->done=c->cancel=c->error=0;
                c->rx_read=c->rx_write=c->tx_read=c->tx_write=0;
                xSemaphoreGive(c->lock);xTaskNotifyGive(c->worker);return;
            }
            xSemaphoreGive(c->lock);
        }
        out->result=SM_BUSY;return;
    }
    if(r->length<sizeof(SmSocketRequest)){out->result=SM_INVALID;return;}
    SmSocketRequest request;memcpy(&request,r->payload,sizeof(request));
    out->result=SM_OFFLINE;
    for(int i=0;i<SM_SOCKET_COUNT;i++) {
        Channel *c=&channels[i];
        if(xSemaphoreTake(c->lock,0)!=pdTRUE){out->result=SM_BUSY;continue;}
        if(c->state==SM_SOCKET_FREE || c->open.token!=request.token){xSemaphoreGive(c->lock);continue;}
        out->result=0;
        unsigned available=c->rx_write-c->rx_read,space=TX_SIZE-(c->tx_write-c->tx_read);
        if(r->op==SM_SOCKET_STATUS) {
            SmSocketStatus s={c->state,available,space,c->error};memcpy(out->payload,&s,sizeof(s));out->length=sizeof(s);
        } else if(r->op==SM_SOCKET_CLOSE) {
            c->cancel=1;if(c->done){c->state=SM_SOCKET_FREE;memset(&c->open,0,sizeof(c->open));}
        } else if(r->op==SM_SOCKET_READ) {
            unsigned n=request.length;
            if(n>SM_PAYLOAD_SIZE)n=SM_PAYLOAD_SIZE;
            if(n>available)n=available;
            if(n){copy_out(out->payload,c->rx,RX_SIZE,c->rx_read,n);c->rx_read+=n;out->length=n;}
            else out->result=c->state==SM_SOCKET_EOF?0:c->state==SM_SOCKET_ERROR?SM_IO:SM_BUSY;
        } else if(r->op==SM_SOCKET_WRITE) {
            unsigned n=request.length;
            if(n>SM_PAYLOAD_SIZE-sizeof(request) || r->length!=sizeof(request)+n)out->result=SM_INVALID;
            else if(c->state!=SM_SOCKET_READY)out->result=c->state==SM_SOCKET_CONNECTING?SM_BUSY:SM_IO;
            else if(space<n)out->result=SM_BUSY;
            else {copy_in(c->tx,TX_SIZE,c->tx_write,r->payload+sizeof(request),n);c->tx_write+=n;out->result=n;}
        } else out->result=SM_INVALID;
        xSemaphoreGive(c->lock);return;
    }
}

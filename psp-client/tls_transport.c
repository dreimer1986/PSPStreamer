#include <pspkernel.h>
#include <pspnet_inet.h>
#include <systemctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <mbedtls/ssl.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/sha256.h>
#include "tls_transport.h"

typedef struct {
    int fd;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_ctr_drbg_context random;
} TlsConnection;
static TlsConnection *connections[8];
static int lock=-1, certificate_lock=-1;
static volatile int notice;
int tls_notice(void) { return notice; }
void tls_notice_clear(void) { notice=0; }
int tls_init(void) {
    lock=sceKernelCreateSema("TLS slots",0,1,1,NULL);
    certificate_lock=sceKernelCreateSema("TLS certificates",0,1,1,NULL);
    return lock<0 || certificate_lock<0 ? -1 : 0;
}
static TlsConnection *lookup(int fd) {
    TlsConnection *result=NULL;
    if(lock<0) return NULL;
    sceKernelWaitSema(lock,1,NULL);
    for(int i=0;i<8;i++) if(connections[i] && connections[i]->fd==fd) {result=connections[i];break;}
    sceKernelSignalSema(lock,1);
    return result;
}
static int would_block(void) {
    int e=sceNetInetGetErrno();
    /* sceNetInet returns BSD errno 35, not newlib's EAGAIN (11). */
    return e==35 || e==EAGAIN || e==EWOULDBLOCK || e==EINTR;
}
static int send_cb(void *ctx,const unsigned char *data,size_t size) {
    TlsConnection *c=ctx;
    int n=sceNetInetSend(c->fd,data,size,0);
    return n<0 ? (would_block()?MBEDTLS_ERR_SSL_WANT_WRITE:MBEDTLS_ERR_SSL_INTERNAL_ERROR) : n;
}
static int recv_cb(void *ctx,unsigned char *data,size_t size) {
    TlsConnection *c=ctx;
    int n=sceNetInetRecv(c->fd,data,size,0);
    return n<0 ? (would_block()?MBEDTLS_ERR_SSL_WANT_READ:MBEDTLS_ERR_SSL_INTERNAL_ERROR) : n;
}
static int wait_io(int fd,int result,int timeout_ms) {
    struct SceNetInetPollfd p={fd,result==MBEDTLS_ERR_SSL_WANT_WRITE?SCE_NET_INET_POLLOUT:SCE_NET_INET_POLLIN,0};
    int n=sceNetInetPoll(&p,1,timeout_ms);
    return n>0 && !(p.revents&p.events)?-1:n;
}
static int retry(int result) { return result==MBEDTLS_ERR_SSL_WANT_READ || result==MBEDTLS_ERR_SSL_WANT_WRITE; }
static unsigned long long now(void) { return sceKernelGetSystemTimeWide(); }
/* PSP newlib's getentropy uses time-seeded MT19937 in this toolchain.
 * Do not use it for TLS keys. ARK exposes the KIRK PRNG to user-mode. */
static int kirk_entropy(void *unused,unsigned char *output,size_t size) {
    (void)unused;
    while(size) {
        unsigned int value=sctrlKernelRand();
        size_t count=size<sizeof(value)?size:sizeof(value);
        memcpy(output,&value,count);output+=count;size-=count;
    }
    return 0;
}
static void destroy(TlsConnection *c) {
    mbedtls_ssl_free(&c->ssl); mbedtls_ssl_config_free(&c->config);
    mbedtls_ctr_drbg_free(&c->random); free(c);
}
/* User-selected opportunistic policy: record the actual handshake certificate,
 * automatically accept replacements and notify. This is NOT CA authentication.
 * Keep each host:port separate. Never read credentials into certificate files. */
static void remember_certificate(TlsConnection *c,const char *host,int port) {
    static char cached_key[96];
    static unsigned char cached_digest[32];
    const mbedtls_x509_crt *crt=mbedtls_ssl_get_peer_cert(&c->ssl);
    unsigned char hash[32], old[32], digest[32];
    char key[96],path[128],temporary[140],hex[65];
    int same=0, existed=0,ok=0;
    FILE *file;
    if(!crt || !crt->raw.len) {notice=3;return;}
    snprintf(key,sizeof(key),"%s:%d",host,port);
    mbedtls_sha256_ret((const unsigned char *)key,strlen(key),hash,0);
    for(int i=0;i<32;i++) snprintf(hex+2*i,3,"%02x",hash[i]);
    snprintf(path,sizeof(path),"certificates/%s.der",hex);
    snprintf(temporary,sizeof(temporary),"%s.tmp",path);
    mbedtls_sha256_ret(crt->raw.p,crt->raw.len,digest,0);
    sceKernelWaitSema(certificate_lock,1,NULL);
    if(!strcmp(cached_key,key) && !memcmp(cached_digest,digest,32)) {
        sceKernelSignalSema(certificate_lock,1);return;
    }
    file=fopen(path,"rb");
    if(file) {
        mbedtls_sha256_context ctx; unsigned char block[512]; size_t n;
        existed=1; mbedtls_sha256_init(&ctx); mbedtls_sha256_starts_ret(&ctx,0);
        while((n=fread(block,1,sizeof(block),file))) mbedtls_sha256_update_ret(&ctx,block,n);
        mbedtls_sha256_finish_ret(&ctx,old); mbedtls_sha256_free(&ctx);
        same=!ferror(file) && !memcmp(old,digest,32); fclose(file);
    }
    if(!same) {
        sceIoMkdir("certificates",0777);
        file=fopen(temporary,"wb");
        if(file) {ok=fwrite(crt->raw.p,1,crt->raw.len,file)==crt->raw.len; if(fclose(file))ok=0;}
        if(ok) { /* FAT rename cannot replace an existing destination. */
            char backup[140]; snprintf(backup,sizeof(backup),"%s.bak",path);
            sceIoRemove(backup);
            if(existed && sceIoRename(path,backup)<0) ok=0;
            if(ok && sceIoRename(temporary,path)<0) {if(existed)sceIoRename(backup,path);ok=0;}
        }
        notice=ok?(existed?2:(notice==2?2:1)):3;
    }
    if(same || ok) {strcpy(cached_key,key);memcpy(cached_digest,digest,32);}
    sceKernelSignalSema(certificate_lock,1);
}
int tls_open(int fd,const char *host,int port,volatile int *running,int timeout_ms) {
    int result=-1,nonblock=1;
    unsigned long long deadline=now()+(unsigned long long)timeout_ms*1000;
    if(lock<0 || certificate_lock<0 || sceNetInetSetsockopt(fd,SOL_SOCKET,SO_NONBLOCK,&nonblock,sizeof(nonblock))<0) return -1;
    TlsConnection *c=calloc(1,sizeof(*c)); if(!c)return -1; c->fd=fd;
    mbedtls_ssl_init(&c->ssl);mbedtls_ssl_config_init(&c->config);
    mbedtls_ctr_drbg_init(&c->random);
    if((result=mbedtls_ctr_drbg_seed(&c->random,kirk_entropy,NULL,
            (const unsigned char *)"PSPStreamer",11))!=0) goto failed;
    if((result=mbedtls_ssl_config_defaults(&c->config,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT))!=0) goto failed;
    mbedtls_ssl_conf_min_version(&c->config,MBEDTLS_SSL_MAJOR_VERSION_3,MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_authmode(&c->config,MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&c->config,mbedtls_ctr_drbg_random,&c->random);
    if((result=mbedtls_ssl_setup(&c->ssl,&c->config))!=0 ||
       (result=mbedtls_ssl_set_hostname(&c->ssl,host))!=0) goto failed;
    mbedtls_ssl_set_bio(&c->ssl,c,send_cb,recv_cb,NULL);
    for(;;) {
        if((running && !*running) || now()>=deadline) {result=-1;goto failed;}
        result=mbedtls_ssl_handshake(&c->ssl);
        if(!result)break;
        if(!retry(result) || wait_io(fd,result,50)<0)goto failed;
    }
    if(!mbedtls_ssl_get_peer_cert(&c->ssl)) {result=-1;goto failed;}
    remember_certificate(c,host,port);
    sceKernelWaitSema(lock,1,NULL);
    for(int i=0;i<8;i++) if(!connections[i]) {
        connections[i]=c;sceKernelSignalSema(lock,1);return 0;
    }
    sceKernelSignalSema(lock,1); result=-1;
failed:
    destroy(c);return result;
}
int tls_recv(int fd,void *buffer,int size,int timeout_ms) {
    TlsConnection *c=lookup(fd); if(!c)return -1;
    unsigned long long deadline=now()+(unsigned long long)timeout_ms*1000;
    for(;;) {
        int n=mbedtls_ssl_read(&c->ssl,buffer,size);
        if(n==MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)return 0;
        if(!retry(n))return n;
        if(now()>=deadline)return -2;
        if(wait_io(fd,n,timeout_ms<50?timeout_ms:50)<0)return -1;
    }
}
int tls_send(int fd,const void *buffer,int size,volatile int *running,int timeout_ms) {
    TlsConnection *c=lookup(fd);if(!c)return -1;
    int sent=0;
    unsigned long long deadline=now()+(unsigned long long)timeout_ms*1000;
    while(sent<size) {
        if((running && !*running) || now()>=deadline)return -1;
        int n=mbedtls_ssl_write(&c->ssl,(const unsigned char *)buffer+sent,size-sent);
        if(retry(n)) {if(wait_io(fd,n,50)<0)return -1;continue;}
        if(n<=0)return -1;
        sent+=n;
    }
    return sent;
}
int tls_close(int fd) {
    TlsConnection *c=lookup(fd);
    if(c) {
        sceKernelWaitSema(lock,1,NULL);
        for(int i=0;i<8;i++)if(connections[i]==c)connections[i]=NULL;
        sceKernelSignalSema(lock,1);
        /* No blocking close-notify during cancellation. */
        destroy(c);
    }
    return sceNetInetClose(fd);
}

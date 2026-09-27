#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <openssl/sha.h>
/* Host SHA implementation is an independent oracle for the PSP scheduling.
 * Production retains Mbed TLS; no cryptographic primitives were changed. */
typedef SHA256_CTX mbedtls_sha256_context;
static void mbedtls_sha256_init(mbedtls_sha256_context *c){memset(c,0,sizeof(*c));}
static int mbedtls_sha256_starts_ret(mbedtls_sha256_context *c,int v){assert(!v);return SHA256_Init(c)?0:-1;}
static int mbedtls_sha256_update_ret(mbedtls_sha256_context *c,const void *p,size_t n){return SHA256_Update(c,p,n)?0:-1;}
static int mbedtls_sha256_finish_ret(mbedtls_sha256_context *c,unsigned char *p){return SHA256_Final(p,c)?0:-1;}
static void mbedtls_sha256_free(mbedtls_sha256_context *c){memset(c,0,sizeof(*c));}
static int download_running=1,debug_enabled=1,fail_allocation;
typedef int SceUID;
#define PSP_O_RDONLY O_RDONLY
#define sceIoOpen open
#define sceIoRead read
#define sceIoWrite write
static unsigned long long sceKernelGetSystemTimeWide(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (unsigned long long)t.tv_sec*1000000+t.tv_nsec/1000;}
static void recovery_flush(void){}
static void recovery_log(const char *e,int r,int h,const char *d){(void)e;(void)r;(void)h;(void)d;}
#define OFFLINE_TEST_READ
#include "offline_async_mock.h"
#include "../psp-client/offline_io.h"
static int sceIoClose(int fd){assert(!io_busy);return close(fd);}
static void *test_malloc(size_t n){return fail_allocation?NULL:malloc(n);}
#define malloc test_malloc
/* HASH */
#undef malloc
int main(int argc,char **argv) {
    assert(argc==4);(void)sceIoWriteAsync;
    int mode=atoi(argv[3]);io_disabled=mode==1;io_fail=mode==2;io_cancel=mode==3;fail_allocation=mode==4;
    int rc=offline_hash(argv[1],argv[2]);assert(!io_busy);
    assert((rc==0)==(mode!=2 && mode!=3 && mode!=5));return 0;
}

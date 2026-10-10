/* Host-only observer checks. Build with -no-pie: the original function address
 * must fit the PSP's 32-bit wire structure. No Sony code is used here. */
#define _GNU_SOURCE
#include <assert.h>
#include <string.h>
#include <sys/mman.h>
#include "xmb_texture_payload.c"
static unsigned calls;
static int return_value;
static int original(const char *key,void **out,unsigned pool,unsigned cache,
                    const void *input,unsigned size,unsigned flags) {
    (void)key;(void)out;(void)input;(void)size;
    assert(pool==123 && cache==456 && flags==7);
    calls++;
    return return_value;
}
int main(void) {
    unsigned char *mem=mmap((void *)0x09000000,4096,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(mem==(void *)0x09000000);
    char *key=(void *)mem;strcpy(key,"42_bg");
    void **out=(void *)(mem+64);
    uint32_t *surface=(void *)(mem+128);
    for(unsigned i=0;i<26;i++)surface[i]=0x12340000+i;
    *out=surface;
    unsigned char *input=mem+256;input[0]='a';
    assert((uintptr_t)original<=UINT32_MAX);
    XmbTextureShared ctx={.original=(uintptr_t)original,.ram_end=0x0c000000,.enabled=1};
    assert(!xmb_texture_payload(key,out,123,456,input,1,7,&ctx));
    assert(calls==1 && ctx.produced==1 && !ctx.users && !ctx.busy);
    assert(!strcmp(ctx.records[0].key,"42_bg"));
    assert(ctx.records[0].input_hash==0xe40c292c);
    assert(ctx.records[0].surface==(uintptr_t)surface);
    assert(!memcmp(ctx.records[0].words,surface,104));
    ctx.enabled=0;
    assert(!xmb_texture_payload(key,out,123,456,input,1,7,&ctx));
    assert(calls==2 && ctx.produced==1 && !ctx.users);
    ctx.enabled=1;return_value=-123;
    assert(xmb_texture_payload(key,out,123,456,input,1,7,&ctx)==-123);
    assert(ctx.records[1].result==(uint32_t)-123 && !ctx.records[1].surface);
    return_value=0;
    assert(!xmb_texture_payload((void *)1,(void *)1,123,456,(void *)1,1,7,&ctx));
    assert(!ctx.records[2].surface && !ctx.records[2].key[0] && !ctx.records[2].input_hash);
    *out=(void *)0x0bfffffc;
    assert(!xmb_texture_payload(key,out,123,456,(void *)0x0bffffff,2,7,&ctx));
    assert(!ctx.records[3].surface && !ctx.records[3].input_hash);
    ctx.busy=1;
    assert(!xmb_texture_payload(key,out,123,456,input,1,7,&ctx));
    assert(ctx.produced==4 && !ctx.users);ctx.busy=0;
    while(ctx.produced<XMB_TEXTURE_RECORDS)
        assert(!xmb_texture_payload(key,out,123,456,input,1,7,&ctx));
    assert(!xmb_texture_payload(key,out,123,456,input,1,7,&ctx));
    assert(ctx.produced==XMB_TEXTURE_RECORDS && ctx.dropped==1 && !ctx.users && !ctx.busy);
    assert(readable(0x08800000,104,0x0c000000));
    assert(!readable(0xffffffff,104,0x0c000000));
    assert(!readable(0x08800000,0xffffffff,0x0c000000));
    munmap(mem,4096);
    return 0;
}

#define _GNU_SOURCE
#include <assert.h>
#include <string.h>
#include <sys/mman.h>
#include "xmb_animation_payload.c"
static unsigned load_calls,bind_calls,destroy_calls;
static const uint32_t *expect_original;
static uint32_t captured[26];
static int original_load(const char *key,void **out,unsigned pool,unsigned cache,const void *in,unsigned n,unsigned f) {
    assert(!strcmp(key,"0_bg") && out && pool==12 && cache==34 && in && n==1 && f==0);
    load_calls++;return 0;
}
static int original_bind(const uint32_t *s,float u,float v,float su,float sv) {
    assert(u==0.125f && v==0.25f && su==0.5f && sv==1.0f);
    if(expect_original)assert(s==expect_original);
    memcpy(captured,s,sizeof(captured));bind_calls++;return 71;
}
static void original_destroy(void *s){assert(s);destroy_calls++;}
int main(void) {
    unsigned char *mem=mmap((void *)0x09000000,4096,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(mem==(void *)0x09000000);
    char *key=(void *)mem;strcpy(key,"0_bg");
    void **out=(void *)(mem+64);uint32_t *s=(void *)(mem+128);*out=s;
    unsigned char *input=mem+256;*input='a';
    s[0]=0x089a04d0;s[2]=3;s[3]=5;s[6]=0x011001e0;s[18]=0x09660000;
    uint32_t saved[26];memcpy(saved,s,sizeof(saved));
    assert((uintptr_t)original_load<=UINT32_MAX && (uintptr_t)original_bind<=UINT32_MAX &&
        (uintptr_t)original_destroy<=UINT32_MAX);
    XmbAnimationShared c={.original_load=(uintptr_t)original_load,.original_bind=(uintptr_t)original_bind,
        .original_destroy={(uintptr_t)original_destroy,(uintptr_t)original_destroy},
        .surface_vtable=s[0],.ram_end=0x0c000000,.active=1};
    c.bank[0]=(XmbAnimationBank){.ready=1,.pixels=0x0a100000,
        .tex_width=512,.tex_height=256,.u_ratio=0x3f200000,.v_ratio=0x3f340000,
        .header={XMB_ANIMATION_MAGIC,1,1,0xe40c292c,320,180,656,120704,48,166666,0,0}};
    XmbAnimationBank *b=&c.bank[0];
    assert(xmb_animation_header_valid(&b->header));
    assert(xmb_animation_ratio(320,512)==b->u_ratio && xmb_animation_ratio(180,256)==b->v_ratio);
    for(unsigned n=8;n<=480;n++){
        unsigned p=1;while(p<n)p<<=1;
        union {float f;uint32_t u;} value={.f=(float)n/p};
        assert(xmb_animation_ratio(n,p)==value.u);
    }
    assert(!xmb_animation_load(key,out,12,34,input,1,0,&c));
    assert(load_calls==1 && c.loads==1 && c.surfaces[0].surface==(uintptr_t)s);
    b->frame=7;
    assert(xmb_animation_bind(s,&c,.125f,.25f,.5f,1.f)==71);
    assert(bind_calls==1 && c.binds==1 && captured[2]==0 && captured[3]==4);
    assert(captured[6]==(320|(180<<16)) && captured[18]==b->pixels+7*120704 && !captured[20]);
    assert(captured[10]==(512<<16) && captured[11]==256);
    assert(captured[12]==b->u_ratio && captured[13]==b->v_ratio);
    assert(b->references==1 && c.users==0 && c.request_hash==b->header.pic1_hash);
    assert(!memcmp(s,saved,sizeof(saved))); /* no native-surface mutation */
    b->frame=UINT32_MAX;
    assert(xmb_animation_bind(s,&c,.125f,.25f,.5f,1.f)==71 && captured[18]==b->pixels && b->references==1);
    c.active=0;expect_original=s;
    assert(xmb_animation_bind(s,&c,.125f,.25f,.5f,1.f)==71 && c.binds==2);
    assert(!xmb_animation_load(key,out,12,34,input,1,0,&c) && c.loads==1);
    c.active=1;*input='b';
    assert(!xmb_animation_load(key,out,12,34,input,1,0,&c) && c.loads==1);
    xmb_animation_destroy(s,&c,0);
    assert(!c.target && c.destroys==1 && destroy_calls==1 && !b->references);
    assert(xmb_animation_bind(s,&c,.125f,.25f,.5f,1.f)==71 && c.binds==2);
    xmb_animation_destroy(s,&c,1);assert(destroy_calls==2 && c.destroys==1);
    /* Reused address is identified afresh and requests a DIFFERENT bank. */
    assert(!xmb_animation_load(key,out,12,34,input,1,0,&c) && c.loads==2);
    assert(xmb_animation_bind(s,&c,.125f,.25f,.5f,1.f)==71 && c.binds==2);
    assert(c.request_hash==0xe70c2de5 && !b->references);
    c.bank[1]=*b;c.bank[1].header.pic1_hash=c.request_hash;c.bank[1].pixels=0x0b000000;
    expect_original=NULL;
    assert(xmb_animation_bind(s,&c,.125f,.25f,.5f,1.f)==71);
    assert(captured[18]==0x0b000000 && c.bank[1].references==1 && !b->references);
    xmb_animation_destroy(s,&c,1);assert(!c.bank[1].references && !c.users);
    XmbAnimationHeader bad=b->header;bad.frames=UINT32_MAX;assert(!xmb_animation_header_valid(&bad));
    bad=b->header;bad.frame_bytes=0;assert(!xmb_animation_header_valid(&bad));
    bad=b->header;bad.width=0;assert(!xmb_animation_header_valid(&bad));
    bad=b->header;bad.period_us=1;assert(!xmb_animation_header_valid(&bad));
    bad=b->header;bad.reserved=1;assert(!xmb_animation_header_valid(&bad));
    munmap(mem,4096);return 0;
}

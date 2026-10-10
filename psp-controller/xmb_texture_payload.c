/* SPDX-License-Identifier: GPL-2.0-or-later
 * Runs in the original VSH user context. No kernel calls, imports, allocation
 * or alteration of PAF objects. The eighth argument is supplied by our stub;
 * the verified original image-cache helper uses seven arguments.
 */
#include "xmb_texture_shared.h"
static __attribute__((always_inline)) inline int readable(uintptr_t p,unsigned n,unsigned end) {
    return p>=0x08800000U && p<end && n<=end-p;
}
int xmb_texture_payload(const char *key,void **out,unsigned pool,unsigned cache,
                        const void *input,unsigned size,unsigned flags,XmbTextureShared *ctx) {
    __sync_fetch_and_add(&ctx->users,1);
    int (*original)(const char *,void **,unsigned,unsigned,const void *,unsigned,unsigned)=(void *)(uintptr_t)ctx->original;
    int result=original(key,out,pool,cache,input,size,flags);
    if(ctx->enabled && __sync_bool_compare_and_swap(&ctx->busy,0,1)) {
        unsigned index=ctx->produced;
        if(index<XMB_TEXTURE_RECORDS){
            XmbTextureRecord *r=&ctx->records[index];
            r->result=result;r->input_size=size;r->flags=flags;r->surface=0;r->input_hash=0;
            for(unsigned i=0;i<32;i++)r->key[i]=0;
            for(unsigned i=0;i<26;i++)r->words[i]=0;
            if(readable((uintptr_t)key,32,ctx->ram_end))
                for(unsigned i=0;i<31 && key[i];i++)r->key[i]=key[i];
            if(size && size<=1024*1024 && readable((uintptr_t)input,size,ctx->ram_end)) {
                const unsigned char *p=input;unsigned hash=2166136261U;
                for(unsigned i=0;i<size;i++)hash=(hash^p[i])*16777619U;
                r->input_hash=hash;
            }
            if(result>=0 && !((uintptr_t)out&3) && readable((uintptr_t)out,sizeof(*out),ctx->ram_end)) {
                uintptr_t surface=(uintptr_t)*out;
                if(!(surface&3) && readable(surface,sizeof(r->words),ctx->ram_end)) {
                    r->surface=surface;
                    const volatile uint32_t *words=(const volatile uint32_t *)surface;
                    for(unsigned i=0;i<26;i++)r->words[i]=words[i];
                }
            }
            __sync_synchronize();ctx->produced=index+1;
        } else ctx->dropped++;
        __sync_synchronize();ctx->busy=0;
    }
    __sync_fetch_and_sub(&ctx->users,1);
    return result;
}

/* SPDX-License-Identifier: GPL-2.0-or-later
 * Self-contained USER code. No I/O, allocation, decoder, kernel calls or
 * original-surface writes. Immutable frames survive all queued GE commands.
 */
#include "xmb_animation_shared.h"
static __attribute__((always_inline)) inline int anim_readable(uintptr_t p,unsigned n,unsigned end) {
    return p>=0x08800000U && p<end && n<=end-p;
}
int xmb_animation_load(const char *key,void **out,unsigned pool,unsigned cache,
                       const void *input,unsigned size,unsigned flags,XmbAnimationShared *c) {
    int (*original)(const char *,void **,unsigned,unsigned,const void *,unsigned,unsigned)=
        (void *)(uintptr_t)c->original_load;
    int rc=original(key,out,pool,cache,input,size,flags);
    if(c->active && rc>=0 && size==c->header.pic1_size &&
       anim_readable((uintptr_t)input,size,c->ram_end) && !((uintptr_t)out&3) &&
       anim_readable((uintptr_t)out,sizeof(*out),c->ram_end)) {
        uint32_t *s=*out;
        if(!((uintptr_t)s&3) && anim_readable((uintptr_t)s,104,c->ram_end) &&
           s[0]==c->surface_vtable && s[6]==0x011001e0 && s[2]==3 && s[3]==5) {
            const unsigned char *p=input;uint32_t hash=2166136261U;
            for(unsigned i=0;i<size;i++)hash=(hash^p[i])*16777619U;
            if(hash==c->header.pic1_hash){
                c->frame=0;__sync_synchronize();
                c->target=(uintptr_t)s;c->loads++;
            }
        }
    }
    return rc;
}
int xmb_animation_bind(const uint32_t *s,XmbAnimationShared *c,float u,float v,float su,float sv) {
    int (*original)(const uint32_t *,float,float,float,float)=(void *)(uintptr_t)c->original_bind;
    if(!c->active || c->target!=(uintptr_t)s || s[0]!=c->surface_vtable ||
       s[6]!=0x011001e0 || s[2]!=3 || s[3]!=5)return original(s,u,v,su,sv);
    /* This call owns a live surface through PAF. Never dereference a saved
     * surface from the worker. Only this stack copy reaches the native binder,
     * whose verified path reads fields and emits commands, retaining no object. */
    uint32_t copy[26];for(unsigned i=0;i<26;i++)copy[i]=s[i];
    unsigned w=c->header.width,h=c->header.height;
    copy[2]=0;copy[3]=4; /* native RGB565, 16 bpp */
    copy[6]=w|(h<<16);copy[7]=2; /* swizzled */
    copy[10]=c->tex_width<<16;copy[11]=c->tex_height;
    copy[12]=c->u_ratio;copy[13]=c->v_ratio;
    copy[14]=c->header.frame_bytes;copy[15]=c->header.stride;
    copy[16]=1;copy[17]=0; /* single slot, read/write indices zero */
    unsigned frame=c->frame;if(frame>=c->header.frames)frame=0;
    copy[18]=c->pixels+frame*c->header.frame_bytes;copy[20]=0;
    c->binds++;
    return original(copy,u,v,su,sv);
}
void xmb_animation_destroy(void *surface,XmbAnimationShared *c,unsigned which) {
    /* Both native surface vtable destructors are intercepted before freeing.
     * This also prevents address reuse from animating an unrelated texture. */
    if(c->target==(uintptr_t)surface){c->target=0;__sync_synchronize();c->destroys++;}
    void (*original)(void *)=(void *)(uintptr_t)c->original_destroy[which&1];
    original(surface);
}

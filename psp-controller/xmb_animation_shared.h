/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XMB_ANIMATION_SHARED_H
#define XMB_ANIMATION_SHARED_H
#include <stdint.h>
#define XMB_ANIMATION_MAGIC 0x31414258U /* XBA1 */
#define XMB_ANIMATION_MAX (8U*1024*1024)
typedef struct {
    uint32_t magic,version,pic1_size,pic1_hash;
    uint32_t width,height,stride,frame_bytes,frames,period_us,data_hash,reserved;
} XmbAnimationHeader;
static inline int xmb_animation_header_valid(const XmbAnimationHeader *h) {
    if(h->magic!=XMB_ANIMATION_MAGIC || h->version!=1 || h->reserved ||
       !h->pic1_size || h->pic1_size>1024000 ||
       h->width<16 || h->width>480 || h->height<8 || h->height>272 ||
       h->frames<2 || h->frames>240 || h->period_us<33333 || h->period_us>1000000)return 0;
    unsigned stride=((h->width+1)*2+15)&~15U;
    unsigned bytes=(stride*((h->height+8)&~7U)+63)&~63U;
    return h->stride==stride && h->frame_bytes==bytes &&
        h->frames<=XMB_ANIMATION_MAX/bytes;
}
typedef struct {
    uint32_t original_load,original_bind,original_destroy[2],surface_vtable,ram_end;
    volatile uint32_t active,target,frame,loads,binds,destroys;
    XmbAnimationHeader header;
    uint32_t pixels,tex_width,tex_height,u_ratio,v_ratio;
} XmbAnimationShared;
/* Exact IEEE-754 n/power-of-two without touching the kernel's FPU context. */
static inline uint32_t xmb_animation_ratio(unsigned n,unsigned p) {
    unsigned a=0,b=0;
    for(unsigned v=n;v>1;v>>=1)a++;
    for(unsigned v=p;v>1;v>>=1)b++;
    return ((127+a-b)<<23)|((n<<(23-a))&0x7fffffU);
}
#endif

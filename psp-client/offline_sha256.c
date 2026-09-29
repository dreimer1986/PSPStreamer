/* SPDX-License-Identifier: GPL-2.0-or-later
 * SHA-256 rounds adapted from Mbed TLS 2.28.10 library/sha256.c.
 * Copyright The Mbed TLS Contributors (Apache-2.0 OR GPL-2.0-or-later).
 * Local adaptation: process whole file blocks in a batch, clearing the work
 * array once per batch instead of once per 64 bytes. Padding/partial blocks
 * and context lifecycle remain the library's responsibility. */
#include "offline_sha256.h"
#include <string.h>
static const uint32_t k[64]={
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
#define ROT(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define S0(x) (ROT(x,7)^ROT(x,18)^((x)>>3))
#define S1(x) (ROT(x,17)^ROT(x,19)^((x)>>10))
#define S2(x) (ROT(x,2)^ROT(x,13)^ROT(x,22))
#define S3(x) (ROT(x,6)^ROT(x,11)^ROT(x,25))
#define ROUND(a,b,c,d,e,f,g,h,j,slot) do { \
    uint32_t t=(h)+S3(e)+((g)^((e)&((f)^(g))))+k[j]+w[slot]; \
    (d)+=t; (h)=t+S2(a)+(((a)&(b))|((c)&((a)|(b)))); \
} while(0)
/* The schedule needs only its previous 16 words. Constant slots avoid the
 * per-word schedule loop/indexing and keep the working set small on Allegrex.
 * Expand immediately before use; unsigned arithmetic wraps modulo 2^32. */
#define EXPAND(j) (w[j]+=S1(w[((j)+14)&15])+w[((j)+9)&15]+S0(w[((j)+1)&15]))
static void blocks(uint32_t state[8],const unsigned char *p,size_t count) {
    uint32_t w[16],a[8];
    while(count--) {
        for(unsigned i=0;i<16;i++,p+=4)w[i]=((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
        memcpy(a,state,sizeof(a));
            ROUND(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],0,0);
            ROUND(a[7],a[0],a[1],a[2],a[3],a[4],a[5],a[6],1,1);
            ROUND(a[6],a[7],a[0],a[1],a[2],a[3],a[4],a[5],2,2);
            ROUND(a[5],a[6],a[7],a[0],a[1],a[2],a[3],a[4],3,3);
            ROUND(a[4],a[5],a[6],a[7],a[0],a[1],a[2],a[3],4,4);
            ROUND(a[3],a[4],a[5],a[6],a[7],a[0],a[1],a[2],5,5);
            ROUND(a[2],a[3],a[4],a[5],a[6],a[7],a[0],a[1],6,6);
            ROUND(a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[0],7,7);
            ROUND(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],8,8);
            ROUND(a[7],a[0],a[1],a[2],a[3],a[4],a[5],a[6],9,9);
            ROUND(a[6],a[7],a[0],a[1],a[2],a[3],a[4],a[5],10,10);
            ROUND(a[5],a[6],a[7],a[0],a[1],a[2],a[3],a[4],11,11);
            ROUND(a[4],a[5],a[6],a[7],a[0],a[1],a[2],a[3],12,12);
            ROUND(a[3],a[4],a[5],a[6],a[7],a[0],a[1],a[2],13,13);
            ROUND(a[2],a[3],a[4],a[5],a[6],a[7],a[0],a[1],14,14);
            ROUND(a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[0],15,15);
        for(unsigned i=16;i<64;i+=16) {
            EXPAND(0); ROUND(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],i+0,0);
            EXPAND(1); ROUND(a[7],a[0],a[1],a[2],a[3],a[4],a[5],a[6],i+1,1);
            EXPAND(2); ROUND(a[6],a[7],a[0],a[1],a[2],a[3],a[4],a[5],i+2,2);
            EXPAND(3); ROUND(a[5],a[6],a[7],a[0],a[1],a[2],a[3],a[4],i+3,3);
            EXPAND(4); ROUND(a[4],a[5],a[6],a[7],a[0],a[1],a[2],a[3],i+4,4);
            EXPAND(5); ROUND(a[3],a[4],a[5],a[6],a[7],a[0],a[1],a[2],i+5,5);
            EXPAND(6); ROUND(a[2],a[3],a[4],a[5],a[6],a[7],a[0],a[1],i+6,6);
            EXPAND(7); ROUND(a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[0],i+7,7);
            EXPAND(8); ROUND(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],i+8,8);
            EXPAND(9); ROUND(a[7],a[0],a[1],a[2],a[3],a[4],a[5],a[6],i+9,9);
            EXPAND(10); ROUND(a[6],a[7],a[0],a[1],a[2],a[3],a[4],a[5],i+10,10);
            EXPAND(11); ROUND(a[5],a[6],a[7],a[0],a[1],a[2],a[3],a[4],i+11,11);
            EXPAND(12); ROUND(a[4],a[5],a[6],a[7],a[0],a[1],a[2],a[3],i+12,12);
            EXPAND(13); ROUND(a[3],a[4],a[5],a[6],a[7],a[0],a[1],a[2],i+13,13);
            EXPAND(14); ROUND(a[2],a[3],a[4],a[5],a[6],a[7],a[0],a[1],i+14,14);
            EXPAND(15); ROUND(a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[0],i+15,15);
        }
        for(unsigned i=0;i<8;i++)state[i]+=a[i];
    }
    volatile uint32_t *erase=w;for(unsigned i=0;i<16;i++)erase[i]=0;
    erase=a;for(unsigned i=0;i<8;i++)erase[i]=0;
}
int offline_sha256_update(mbedtls_sha256_context *ctx,const unsigned char *data,size_t size) {
    unsigned partial=ctx->total[0]&63;
    if(partial) {
        size_t n=64-partial;if(n>size)n=size;
        int rc=mbedtls_sha256_update_ret(ctx,data,n);if(rc)return rc;
        data+=n;size-=n;
    }
    size_t bytes=size&~(size_t)63;
    if(bytes) {
        blocks(ctx->state,data,bytes/64);
        uint32_t old=ctx->total[0];ctx->total[0]+=(uint32_t)bytes;
        if(ctx->total[0]<old)ctx->total[1]++;
#if SIZE_MAX > UINT32_MAX
        ctx->total[1]+=(uint32_t)(bytes>>32);
#endif
        data+=bytes;size-=bytes;
    }
    return size?mbedtls_sha256_update_ret(ctx,data,size):0;
}

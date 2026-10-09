/* SPDX-License-Identifier: GPL-2.0-or-later
 * Optional hardware TEXT dumps remain local, never part of the source tree.
 * Usage: harness game-text.bin vsh-text.bin (known probe load addresses).
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../psp-controller/audio_mirror_signature.h"
#include "../psp-controller/audio_mirror_src.h"
#include "../psp-controller/audio_mirror_flow.h"
static void check(const char *path,uint32_t base,uint32_t state) {
    FILE *f=fopen(path,"rb");assert(f);
    uint32_t words[13588/4];assert(fread(words,1,sizeof(words),f)==sizeof(words));assert(fgetc(f)==EOF);fclose(f);
    assert(mirror_signature(words,sizeof(words),base,state));
    assert(mirror_src_signature(words,sizeof(words),base,state));
    assert(!mirror_src_signature(words,sizeof(words),base,state+64));
    for(unsigned off=0x20ec;off<0x2454;off+=4) {
        words[off/4]^=0x100;
        assert(!mirror_src_signature(words,sizeof(words),base,state));
        words[off/4]^=0x100;
    }
    assert(!mirror_signature(words,sizeof(words)-4,base,state));
    assert(!mirror_signature(words,sizeof(words),base+256,state));
    assert(!mirror_signature(words,sizeof(words),base,state+64));
    assert(!mirror_signature(words,sizeof(words),0x08800000,state));
    for(unsigned off=0x2b8;off<0x530;off+=4) {
        words[off/4]^=0x100;
        assert(!mirror_signature(words,sizeof(words),base,state));
        words[off/4]^=0x100;
    }
    words[0x2f14/4]=1;assert(!mirror_signature(words,sizeof(words),base,state));
}
int main(int argc,char **argv) {
    /* Compare optimized block copies with the original sample-wise ring
     * indexing, including full-size SRC submissions and counter wrap. */
    unsigned ring[MIRROR_SRC_RING],input[MIRROR_SRC_RING],output[MIRROR_SRC_RING];
    for(unsigned i=0;i<MIRROR_SRC_RING;i++)input[i]=i*65537U+17;
    const unsigned positions[]={0,1,4095,4096,8191,0xfffffff0U};
    const unsigned counts[]={0,1,64,960,2048,4096,4111,8192};
    for(unsigned p=0;p<sizeof(positions)/sizeof(*positions);p++)
        for(unsigned n=0;n<sizeof(counts)/sizeof(*counts);n++) {
            memset(ring,0,sizeof(ring));memset(output,0,sizeof(output));
            mirror_ring_write(ring,MIRROR_SRC_RING,positions[p],input,counts[n]);
            for(unsigned i=0;i<counts[n];i++)assert(ring[(positions[p]+i)&(MIRROR_SRC_RING-1)]==input[i]);
            mirror_ring_read(output,ring,MIRROR_SRC_RING,positions[p],counts[n]);
            assert(!memcmp(input,output,counts[n]*4));
        }
    /* The unchanged normal 4096-frame ring uses the same transport fast path. */
    mirror_ring_write(ring,MIRROR_RING,4000,input,960);
    mirror_ring_read(output,ring,MIRROR_RING,4000,960);
    assert(!memcmp(input,output,960*4));
    assert(mirror_src_rate(32000)==32000 && mirror_src_rate(44100)==44100);
    assert(mirror_src_rate(8000)==48000 && !mirror_src_rate(12345));
    assert(mirror_src_sample(0x7fff8000,0x8000)==0x7fff8000);
    assert(!mirror_src_sample(0x7fff8000,0));
    assert(mirror_src_sample(0x4000c000,0xfffff)==0x7fff8000);
    assert(mirror_src_lerp(0x00000000,0x4000c000,24000,48000)==0x2000e000);
    /* 22.05 kHz -> 48 kHz keeps rational phase over arbitrary packets. */
    unsigned phase=0,consumed=0;
    for(unsigned i=0;i<48000;i++){phase+=22050;consumed+=phase/48000;phase%=48000;}
    assert(consumed==22050 && !phase);
    assert(mirror_fresh_burst(4096,4096,256));
    assert(!mirror_fresh_burst(4096,4096,0));
    assert(!mirror_fresh_burst(4352,4096,256));
    assert(mirror_fresh_burst(0,0,960));
    assert(!mirror_fresh_burst(64,0xffffffc0U,256));
    /* A new burst after a long idle must not expire immediately. An existing
     * backlog must still time out even if more frames are being submitted. */
    unsigned long long now=9000000,progress=1000000;
    if(mirror_fresh_burst(4096,4096,256))progress=now;
    assert(now-progress<2000000);
    now+=2100000;
    if(mirror_fresh_burst(4352,4096,256))progress=now;
    assert(now-progress>2000000);
    unsigned reply=0,wake=0;
    assert(mirror_rpc_timing(100,110,130,150,&reply,&wake) && reply==30 && wake==20);
    assert(mirror_rpc_timing(100,140,130,150,&reply,&wake) && reply==30 && wake==10);
    assert(mirror_rpc_timing(0xfffffff0U,0xfffffff8U,0x10,0x20,&reply,&wake) && reply==32 && wake==16);
    assert(!mirror_rpc_timing(100,99,130,150,&reply,&wake));
    assert(!mirror_rpc_timing(100,110,151,150,&reply,&wake));
    /* Exercise backlog thresholds and ring-index wrap without enlarging the
     * allocation or resetting the optical session on capture overflow. */
    assert(mirror_trim_frames(0)==0);
    assert(mirror_trim_frames(3072)==0);
    assert(mirror_trim_frames(3136)==0);
    assert(mirror_trim_frames(3840)==0);
    assert(mirror_trim_frames(MIRROR_RING)==512);
    assert(mirror_packet_frames(4096,0,4096)==960);
    assert(mirror_packet_frames(320,0,4096)==320);
    assert(mirror_packet_frames(320,2048,4096)==0);
    assert(mirror_packet_frames(4096,3072,4096)==0);
    assert(mirror_packet_frames(4096,0,128)==0);
    for(unsigned available=0;available<=4096;available+=31)
        for(unsigned queued=0;queued<=4096;queued+=63)
            for(unsigned space=0;space<=4096;space+=127) {
                unsigned n=mirror_packet_frames(available,queued,space);
                assert(n<=available && n<=space && n<=960 && !(n&63));
                if(n)assert(queued+n<=3072);
            }
    for(unsigned rd=0xfffff000U;rd!=0;rd+=64) {
        for(unsigned available=0;available<=MIRROR_RING;available+=64) {
            unsigned wr=rd+available;
            unsigned next=rd+mirror_trim_frames(wr-rd);
            assert(wr-next<=3840);
            assert((next&63)==0);
            if(available>3840)assert(wr-next==3584);
            else assert(next==rd);
        }
    }
    uint32_t physical=0x0813d180;
    assert(mirror_dma_half(0,physical+1024,physical)==0);
    assert(mirror_dma_half(physical+1056,0,physical)==1);
    assert(mirror_dma_half(0,0,physical)==-1);
    assert(mirror_dma_half(physical+1056,physical+1024,physical)==-1);
    assert(mirror_dma_half(0,physical+1056,physical)==-1);
    assert(mirror_dma_half(physical+1024,0,physical)==-1);
    if(argc==3){check(argv[1],0x88139c00,0x8813d180);check(argv[2],0x8817c500,0x8817fa80);}
    else assert(argc==1);
    puts("PCM half selection and supplied driver signatures: OK");
    return 0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later
 * Optional hardware TEXT dumps remain local, never part of the source tree.
 * Usage: harness game-text.bin vsh-text.bin (known probe load addresses).
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../psp-controller/audio_mirror_signature.h"
#include "../psp-controller/audio_mirror_flow.h"
static void check(const char *path,uint32_t base,uint32_t state) {
    FILE *f=fopen(path,"rb");assert(f);
    uint32_t words[13588/4];assert(fread(words,1,sizeof(words),f)==sizeof(words));assert(fgetc(f)==EOF);fclose(f);
    assert(mirror_signature(words,sizeof(words),base,state));
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

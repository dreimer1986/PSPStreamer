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
    /* Exercise backlog thresholds and ring-index wrap without enlarging the
     * allocation or resetting the optical session on capture overflow. */
    assert(mirror_trim_frames(0)==0);
    assert(mirror_trim_frames(3072)==0);
    assert(mirror_trim_frames(3136)==2112);
    assert(mirror_trim_frames(MIRROR_RING)==3072);
    for(unsigned rd=0xfffff000U;rd!=0;rd+=64) {
        for(unsigned available=0;available<=MIRROR_RING;available+=64) {
            unsigned wr=rd+available;
            unsigned next=rd+mirror_trim_frames(wr-rd);
            assert(wr-next<=3072);
            assert((next&63)==0);
            if(available>3072)assert(wr-next==1024);
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

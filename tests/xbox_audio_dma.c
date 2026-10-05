#include <assert.h>
#include <stdio.h>
#include "../xbox-client/audio_dma_cursor.h"
int main(void){
    unsigned known=0;
    assert(xbox_dma_completed(30,&known)==30);
    assert(xbox_dma_completed(0,&known)==30);
    assert(xbox_dma_completed(28,&known)==30);
    assert(xbox_dma_completed(31,&known)==31);
    assert(xbox_dma_completed(32,&known)==32);
    /* Active DMA, pauses, reset and an earlier descriptor are not EOF. */
    assert(xbox_dma_remaining(1200,0,1,7,9)==1200);
    assert(xbox_dma_remaining(1200,2,1,9,9)==1200);
    assert(xbox_dma_remaining(1200,3,0,9,9)==1200);
    assert(xbox_dma_remaining(1200,1,1,9,9)==1200);
    assert(xbox_dma_remaining(1200,3,1,8,9)==1200);
    for(unsigned i=0;i<32;i++){
        assert(xbox_dma_remaining(2304,3,0x1d,i,i)==0);
        assert(xbox_dma_remaining(0,3,0x1d,i,i)==0);
        assert(xbox_dma_remaining(700,3,0x1d,(i+1)%32,i)==700);
        unsigned tail=64+i;
        assert(!xbox_dma_tail_reached(tail-1,tail,tail,0,1,1));
        assert(xbox_dma_tail_reached(tail,tail,tail,0,1,1));
        assert(xbox_dma_tail_reached(tail-31,tail,tail,1,1,1));
        assert(xbox_dma_tail_reached(0,tail,tail,1,1,1));
        assert(!xbox_dma_tail_reached(tail-31,tail,tail,1,1,0));
        assert(!xbox_dma_tail_reached(tail-31,tail,tail,1,0,1));
        assert(!xbox_dma_tail_reached(tail-1,tail-1,tail,1,1,1));
        assert(!xbox_dma_tail_reached(0,0,0,3,1,1));
    }
    puts("PASS: AC97 drain and silent tail; running, pause, reset, wrap and prefetch cases");
}

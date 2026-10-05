#include <assert.h>
#include <stdio.h>
#include "../xbox-client/audio_dma_cursor.h"
int main(void){
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
    }
    puts("PASS: AC97 end-of-list drain versus running/paused/reset/wrapped descriptors");
}

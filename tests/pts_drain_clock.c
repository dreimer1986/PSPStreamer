#include <assert.h>
#include "pts_drain.h"
int main(void) {
    PtsDrainClock c={0};
    /* Ordinary WLAN gap and unplayed PCM must not activate the drain. */
    assert(!pts_drain_update(&c,1000000,1,1000,1026,0,1,1,1));
    assert(!pts_drain_update(&c,1000000,1,1000,1026,1,1,1,0));
    assert(!pts_drain_update(&c,1000000,1,1000,1026,1,1,0,1));
    assert(pts_drain_update(&c,1000000,1,1000,1026,1,1,1,1));
    assert(pts_drain_time(&c,2000000)==2026);
    c.tick+=3000000; /* Pausing must freeze the continuation clock. */
    assert(pts_drain_time(&c,5000000)==2026);
    assert(!pts_drain_update(&c,5000000,1,2000,2026,1,1,1,1));
    /* Simulate a bounded shared reader with 400 trailing video frames. */
    c=(PtsDrainClock){0};
    int read=128, consumed=0;
    for(unsigned long long now=0;consumed<400 && now<30000000;now+=1000) {
        int blocked=read-consumed==128;
        pts_drain_update(&c,now,1,0,26,blocked,1,1,1);
        if(c.active && consumed*50<=pts_drain_time(&c,now))consumed++;
        if(read<400 && read-consumed<128)read++;
    }
    assert(read==400 && consumed==400); /* Reader can reach genuine EOF. */
    return 0;
}

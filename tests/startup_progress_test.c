#include <assert.h>
#include "../psp-controller/startup_progress.h"
int main(void) {
    StartupProgress p={0};
    assert(!startup_audio_stalled(&p,5000000,0));
    assert(!startup_audio_stalled(&p,7000000,96));
    assert(!startup_audio_stalled(&p,24999999,96));
    assert(startup_audio_stalled(&p,25000000,96));
    assert(!startup_audio_stalled(&p,25000001,97));
    assert(!startup_audio_stalled(&p,35000000,97));
    assert(startup_audio_stalled(&p,35000001,97));
    p=(StartupProgress){0};
    assert(!startup_audio_stalled(&p,0,0));
    assert(startup_audio_stalled(&p,20000000,0));
    p=(StartupProgress){0};
    for(unsigned i=0;i<100;i++)assert(!startup_audio_stalled(&p,(unsigned long long)i*1000000,i));
    assert(!startup_audio_stalled(&p,100000000,0)); /* Counter reset is progress. */
    return 0;
}

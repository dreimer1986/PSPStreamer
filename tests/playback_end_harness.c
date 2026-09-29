#include <assert.h>
#include <stdio.h>
#include "playback_end.h"
int main(void) {
    assert(playback_end_allowed(1,0,30,179950,180.0f));
    assert(!playback_end_allowed(0,0,30,179950,180.0f)); /* timeout, no EOF */
    assert(!playback_end_allowed(1,-1320,30,179950,180.0f)); /* error races EOF */
    assert(!playback_end_allowed(1,1,30,180100,180.0f)); /* near end is not proof */
    assert(!playback_end_allowed(1,0,0,179950,180.0f)); /* failed resume near EOF */
    assert(!playback_end_allowed(1,0,30,45000,180.0f)); /* early clean TCP close */
    assert(playback_end_allowed(1,0,30,45000,0.0f)); /* local/unknown duration */
    assert(!playback_end_allowed(1,1,30,45000,0.0f));
    assert(!playback_end_allowed(1,0,0,45000,0.0f));
    puts("Media end: errors, incomplete reconnects and early EOF cannot advance");
}

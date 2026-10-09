#include <assert.h>
#include "../psp-controller/vsh_audio_start.h"
int main(void) {
    assert(!consolizer_vsh_audio_requested(""));
    assert(consolizer_vsh_audio_requested("vsh=1\naudio_mirror=1\n"));
    assert(!consolizer_vsh_audio_requested("enabled=0\nvsh=1\naudio_mirror=1"));
    assert(!consolizer_vsh_audio_requested("vsh=0\naudio_mirror=1"));
    assert(!consolizer_vsh_audio_requested("vsh=1\naudio_mirror=0"));
    assert(consolizer_vsh_audio_requested(" vsh=1\r\n\taudio_mirror=1 \r\n"));
    assert(!consolizer_vsh_audio_requested("#vsh=1\naudio_mirror=1"));
    assert(!consolizer_vsh_audio_requested("vsh=1\naudio_mirror=1\naudio_mirror=0"));
    assert(!consolizer_vsh_audio_requested("vsh=1\naudio_mirror=10"));
    return 0;
}

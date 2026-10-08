/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include "../psp-controller/audio_probe_stats.h"
int main(void) {
    AudioProbeStats s={0};int channels[8]={64,0,0,128,0,0,0,0};
    audio_probe_observe(&s,channels,(int)0x80260008U);
    assert(s.samples==1 && s.normal_hits[0]==1 && s.normal_hits[3]==1);
    assert(s.normal_peak[3]==128 && s.src_unreserved==1 && !s.src_errors);
    channels[0]=32;channels[3]=0;channels[7]=-1;
    audio_probe_observe(&s,channels,4096);
    assert(s.normal_hits[0]==2 && s.normal_peak[0]==64 && s.normal_errors==1);
    assert(s.normal_last_error==-1 && s.src_hits==1 && s.src_peak==4096);
    memset(channels,0,sizeof(channels));audio_probe_observe(&s,channels,0);
    audio_probe_observe(&s,channels,-1);
    assert(s.src_empty==1 && s.src_errors==1 && s.src_last_error==-1);
    uint32_t starts[4]={0x88010000U,0x88030000U,0,0};unsigned sizes[4]={0x8000,0x1000,0,0};
    assert(audio_probe_text_valid(0x88010000U,0x8000,starts,sizes,2));
    assert(!audio_probe_text_valid(0x88010000U,0x8001,starts,sizes,2));
    assert(!audio_probe_text_valid(0x88010001U,8,starts,sizes,2));
    assert(!audio_probe_text_valid(0x88020000U,8,starts,sizes,2));
    assert(!audio_probe_text_valid(0x88010000U,0,starts,sizes,2));
    assert(!audio_probe_text_valid(0x88010000U,8,starts,sizes,5));
    assert(!audio_probe_text_valid(0x08810000U,8,starts,sizes,2));
    assert(!audio_probe_text_valid(0xfffffff0U,64,starts,sizes,2));
    assert(!audio_probe_text_valid(0x88010000U,0xffffffffU,starts,sizes,2));
    assert(!audio_probe_text_valid(0x88010000U,128U*1024+1,starts,sizes,2));
    return 0;
}

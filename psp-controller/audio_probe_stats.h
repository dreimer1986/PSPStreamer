/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef CONSOLIZER_AUDIO_PROBE_STATS_H
#define CONSOLIZER_AUDIO_PROBE_STATS_H
#include <stdint.h>
#include <string.h>

/* Observation only: a zero queue at a sample instant does not prove that a
 * channel is unused. Never reserve a channel merely to identify its owner. */
typedef struct {
    unsigned samples, normal_hits[8], normal_peak[8], normal_errors;
    unsigned src_hits, src_peak, src_empty, src_unreserved, src_errors;
    int normal_last_error, src_last_error;
} AudioProbeStats;
static inline void audio_probe_observe(AudioProbeStats *s,const int normal[8],int src) {
    s->samples++;
    for(unsigned i=0;i<8;i++) {
        if(normal[i]>0) {
            s->normal_hits[i]++;
            if((unsigned)normal[i]>s->normal_peak[i])s->normal_peak[i]=(unsigned)normal[i];
        } else if(normal[i]<0) {s->normal_errors++;s->normal_last_error=normal[i];}
    }
    if(src>0) {s->src_hits++;if((unsigned)src>s->src_peak)s->src_peak=(unsigned)src;}
    else if(src==0)s->src_empty++;
    else if((uint32_t)src==0x80260008U)s->src_unreserved++;
    else {s->src_errors++;s->src_last_error=src;}
}
/* Verify the range against the loader's segment map before reading code. */
static inline int audio_probe_text_valid(uint32_t address,uint32_t size,
                                         const uint32_t starts[4],const unsigned sizes[4],unsigned count) {
    if((address&3) || address<0x88000000U || address>=0x8c000000U ||
       !size || size>128U*1024 || size>0x8c000000U-address || count>4)return 0;
    for(unsigned i=0;i<count;i++)
        if(address>=starts[i] && address-starts[i]<=sizes[i] && size<=sizes[i]-(address-starts[i]))return 1;
    return 0;
}
#endif

/* SPDX-License-Identifier: GPL-2.0-or-later
 * Bounded snapshots of existing analysis only: no PCM, FFT or audio calls. */
#include "milkdrop_signal.h"
#include <string.h>
#include <math.h>
void md_signal_reset(MdSignalState *state) { memset(state, 0, sizeof(*state)); }
void md_signal_update(MdSignalState *s, const unsigned char bands[12],
                      int level, unsigned long long now) {
    float blend = 1;
    int fresh = !s->ready || now < s->tick;
    float steps = fresh ? 0 : (float)(now-s->tick)/1000000.0f*30;
    if (fresh) s->origin = now;
    /* MilkDrop 2's attack/release and history retention at a 30 Hz reference.
     * PSP uses elapsed warm-up time, seeded display bands, not its raw FFT. */
    float attack = powf(.2f, steps), release = powf(.5f, steps);
    float history = powf(now-s->origin < 1666667 ? .9f : .992f, steps);
    if (s->ready && now >= s->tick) {
        /* 250 ms time constant independent of adaptive render frequency.
         * Large gaps settle immediately; no catch-up loop. */
        unsigned long long elapsed = now-s->tick;
        blend = elapsed >= 5000000 ? 1 : -expm1f(-(float)elapsed/250000.0f);
    }
    for (int group = 0; group < 3; group++) {
        int sum = 0;
        for (int i = group*4; i < group*4+4; i++)
            sum += bands[i] > 100 ? 100 : bands[i];
        float raw = (float)sum/400.0f;
        s->signal.values[group] = raw;
        s->signal.values[group+4] += blend*(raw-s->signal.values[group+4]);
        if (fresh) s->average[group] = s->long_average[group] = raw;
        else {
            float rate = raw > s->average[group] ? attack : release;
            s->average[group] = rate*s->average[group] + (1-rate)*raw;
            s->long_average[group] = history*s->long_average[group] + (1-history)*raw;
        }
        float baseline = s->long_average[group];
        s->signal.values[7+group] = baseline < .001f ? 1 : raw/baseline;
        s->signal.values[10+group] = baseline < .001f ? 1 : s->average[group]/baseline;
    }
    s->signal.values[3] = (float)(level < 0 ? 0 : level > 100 ? 100 : level)/100.0f;
    s->tick = now; s->ready = 1;
}

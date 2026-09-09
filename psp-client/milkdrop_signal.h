/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PSPSTREAMER_MILKDROP_SIGNAL_H
#define PSPSTREAMER_MILKDROP_SIGNAL_H
enum { MD_SIGNAL_COUNT = 13 };
/* First seven: native low,mid,high,level and three smooth values, all 0..1.
 * Next six: bass,mid,treb,bass_att,mid_att,treb_att, relative to history.
 * The measurement source is PSP display bins, not MilkDrop's FFT. */
typedef struct { float values[MD_SIGNAL_COUNT]; } MdSignal;
typedef struct {
    MdSignal signal;
    unsigned long long tick;
    int ready;
    float average[3], long_average[3];
    unsigned long long origin;
} MdSignalState;
void md_signal_reset(MdSignalState *state);
void md_signal_update(MdSignalState *state, const unsigned char bands[12],
                      int level, unsigned long long now);
#endif

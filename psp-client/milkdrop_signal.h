/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PSPSTREAMER_MILKDROP_SIGNAL_H
#define PSPSTREAMER_MILKDROP_SIGNAL_H
enum { MD_SIGNAL_COUNT = 7 };
/* Native display inputs, NOT MilkDrop's relative bass/mid/treb analysis.
 * low, mid, high, level, low_smooth, mid_smooth, high_smooth: all 0..1. */
typedef struct { float values[MD_SIGNAL_COUNT]; } MdSignal;
typedef struct {
    MdSignal signal;
    unsigned long long tick;
    int ready;
} MdSignalState;
void md_signal_reset(MdSignalState *state);
void md_signal_update(MdSignalState *state, const unsigned char bands[12],
                      int level, unsigned long long now);
#endif

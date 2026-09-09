/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PSPSTREAMER_MILKDROP_WAVE_H
#define PSPSTREAMER_MILKDROP_WAVE_H
#include "milkdrop_warp.h"
enum { MD_WAVE_SAMPLES=576, MD_WAVE_VERTICES=241 };
extern volatile int md_wave_capture;
/* One decoder producer, one lower-priority UI consumer; neither waits. */
void visualization_pcm_publish(const short *stereo, int frames);
int md_wave_snapshot(short right[MD_WAVE_SAMPLES]);
void md_wave_forget(void);
void md_wave_circle(MdVertex *vertices, const short *right, float scale,
                    float smoothing, float seconds, float aspect, unsigned int color);
#endif

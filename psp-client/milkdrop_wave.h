/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PSPSTREAMER_MILKDROP_WAVE_H
#define PSPSTREAMER_MILKDROP_WAVE_H
#include "milkdrop_warp.h"
#include "milkdrop_decor.h"
enum { MD_WAVE_SAMPLES=576, MD_WAVE_VERTICES=241 };
/* 0: disabled, 1: right channel only, 2: coherent stereo snapshot. */
extern volatile int md_wave_capture;
/* One decoder producer, one lower-priority UI consumer; neither waits. */
void visualization_pcm_publish(const short *stereo, int frames);
int md_wave_snapshot(short right[MD_WAVE_SAMPLES]);
int md_wave_snapshot_stereo(short right[MD_WAVE_SAMPLES], short left[MD_WAVE_SAMPLES]);
int md_wave_script(MdVertex *vertices, const short *right, const short *left,
                   float scale, float smoothing, unsigned int color, const MdDecor *decor);
int md_wave_spiral(MdVertex *vertices, const short *right, const short *left,
                   float scale, float smoothing, float seconds, float aspect,
                   unsigned int color, const MdDecor *decor);
void md_wave_forget(void);
void md_wave_circle(MdVertex *vertices, const short *right, float scale,
                    float smoothing, float seconds, float aspect, unsigned int color);
void md_wave_circle_style(MdVertex *vertices, const short *right, float scale,
                    float smoothing, float seconds, float aspect, unsigned int color,
                    const MdDecor *decor);
#endif

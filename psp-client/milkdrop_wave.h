/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PSPSTREAMER_MILKDROP_WAVE_H
#define PSPSTREAMER_MILKDROP_WAVE_H
#include "milkdrop_warp.h"
#include "milkdrop_decor.h"
enum { MD_WAVE_SAMPLES=576, MD_WAVE_VERTICES=241 };
enum { MD_WAVE_MAX_VERTICES=480, MD_SPECTRUM_SAMPLES=1024 };
/* 0: disabled, 1: right only, 2: coherent stereo, 3: 1024 left FFT samples,
 * 4: stereo PCM plus left FFT source in the same publication. */
extern volatile int md_wave_capture;
/* One decoder producer, one lower-priority UI consumer; neither waits. */
void visualization_pcm_publish(const short *stereo, int frames);
int md_wave_snapshot(short right[MD_WAVE_SAMPLES]);
int md_wave_snapshot_stereo(short right[MD_WAVE_SAMPLES], short left[MD_WAVE_SAMPLES]);
int md_spectrum_snapshot(short left[MD_SPECTRUM_SAMPLES]);
/* Kind 4 carries both stereo PCM and FFT source in one coherent snapshot. */
int md_wave_snapshot_combined(short *right,short *left,short *spectrum);
int md_wave_extra(MdVertex *vertices,int mode,const short *right,const short *left,
                  const short *spectrum,float scale,float smoothing,float seconds,
                  float aspect,unsigned int color,const MdDecor *decor,int *split);
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

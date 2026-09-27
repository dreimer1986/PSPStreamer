/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PSPSTREAMER_SPECTRUM_ANALYSIS_H
#define PSPSTREAMER_SPECTRUM_ANALYSIS_H
enum { SPECTRUM_MAX_BANDS=64 };
extern int spectrum_analysis_mode, spectrum_band_count, spectrum_tv_band_count;
extern int spectrum_gain_db;
extern int spectrum_style,spectrum_segments,spectrum_peak_hold;
void spectrum_analysis_output(int tv);
int spectrum_band_valid(int count);
int spectrum_band_choice(int count,int direction);
int spectrum_bar_count(void);
/* Producer only copies; all transforms run in the music/UI consumer. */
void spectrum_pcm_publish(const short *pcm,int frames);
void spectrum_analysis_reset(void);
/* needed: 0 = idle, 1 = MilkDrop/display, 2 = Monkey reference analysis. */
void spectrum_analysis_step(unsigned long long now,int needed,int playing);
int spectrum_bar_level(int band,int legacy);
const float *spectrum_relative(void);
/* NULL unless a playing Monkey FFT snapshot is available. */
const float *spectrum_monkey_raw(void);
#endif

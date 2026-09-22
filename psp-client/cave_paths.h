/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PSPSTREAMER_CAVE_PATHS_H
#define PSPSTREAMER_CAVE_PATHS_H
enum {CAVE_PATHS=16,CAVE_PATH_CACHE=19};
typedef struct {float x[CAVE_PATHS],y[CAVE_PATHS],radius[CAVE_PATHS];int index;} CavePathFrame;
typedef struct {
    float seed,phase[CAVE_PATHS][2],radius[CAVE_PATHS];
    int next;
    CavePathFrame frames[CAVE_PATH_CACHE];
} CavePaths;
void cave_paths_init(CavePaths *paths,unsigned seed);
/* Recovered base oscillator branch. Alternate path blend/jitter not included. */
void cave_paths_step(CavePaths *paths);
int cave_paths_sample(const CavePaths *paths,float z,CavePathFrame *out,CavePathFrame *derivative);
#endif

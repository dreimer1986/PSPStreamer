/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PSPSTREAMER_CAVE_PATHS_H
#define PSPSTREAMER_CAVE_PATHS_H
enum {CAVE_PATHS=16,CAVE_PATH_CACHE=19};
typedef struct {float x[CAVE_PATHS],y[CAVE_PATHS],radius[CAVE_PATHS];int index;} CavePathFrame;
typedef struct {float time[4],value[4];int ready;} CaveSpline;
typedef struct {
    float seed,phase[CAVE_PATHS][2],radius[CAVE_PATHS];
    int next;
    unsigned random;
    float roughness;
    CaveSpline alternate[CAVE_PATHS][3];
    CavePathFrame frames[CAVE_PATH_CACHE];
} CavePaths;
void cave_paths_init(CavePaths *paths,unsigned seed);
/* Recovered oscillator/spline blend and odd-path jitter. */
void cave_paths_step(CavePaths *paths);
float cave_path_shape(float value,float amount);
float cave_spline_sample(CaveSpline *s,unsigned *random,float t,float interval,float spread,float shape);
void cave_paths_perturb(CavePathFrame *frame,unsigned *random,float amount);
int cave_paths_sample(const CavePaths *paths,float z,CavePathFrame *out,CavePathFrame *derivative);
#endif

#ifndef PSPSTREAMER_MILKDROP_PRESET_H
#define PSPSTREAMER_MILKDROP_PRESET_H
#include "milkdrop_warp.h"
#include "preset_math.h"
#include "milkdrop_signal.h"
#include "milkdrop_decor.h"
typedef struct { PmProgram init, frame; PmSymbols symbols; } MdShapeProgram;
typedef struct { int ready; float t[8], user[PM_USER_COUNT]; PmRuntime runtime; } MdShapeState;
/* Requests above Desktop's 512 points use interpolated input, not extra PCM. */
enum { MD_CUSTOM_WAVES=4, MD_CUSTOM_POINTS=1024 };
typedef struct {
    float enabled,samples,sep,spectrum,dots,thick,additive,scaling,smoothing,r,g,b,a;
    PmProgram init,frame,point; PmSymbols symbols,point_symbols;
} MdCustomWave;
typedef struct { MdShapeState frame; float point_user[PM_USER_COUNT]; PmRuntime point_runtime; } MdWaveState;
typedef struct { int count; MdVertex vertices[MD_CUSTOM_POINTS]; } MdWaveGeometry;
typedef struct {
    MdPreset warp; float red, green, blue; PmProgram program;
    int legacy, wave_mode, wrap;
    float gamma, wave_scale, wave_smoothing, wave_alpha;
    MdDecor decor;
    PmProgram init_program;
    PmSymbols symbols;
    PmProgram pixel_program;
    float motion[9]; /* alpha, RGB, grid X/Y, offsets X/Y, length */
    MdShapeProgram shape_program[MD_SHAPES];
    MdCustomWave waves[MD_CUSTOM_WAVES];
    float effects[5]; /* darken center, brighten, darken, solarize, invert */
    int shape_instances[MD_SHAPES];
    PmSymbols pixel_symbols;
    char texture_path[MD_SHAPES][512]; /* PSP PNG shape extension, preset-relative. */
    float shader_amount; /* Legacy fixed-function hue shading, not HLSL. */
} MdFilePreset;
/* Per-activation seeds. Frame q writes never accumulate into these seeds. */
typedef struct { int ready; float q[PM_Q_COUNT], user[PM_USER_COUNT], frame_q[PM_Q_COUNT];
    unsigned int frames; float last_seconds, fps;
    int wave_mode; float motion[9];
    MdShapeState shape[MD_SHAPES];
    MdWaveState waves[MD_CUSTOM_WAVES];
    float effects[5];
    PmRuntime runtime,pixel_runtime;
    float pixel_user[PM_USER_COUNT],monitor;
    int wrap;
} MdPresetState;
enum { MD_FILE_OK, MD_FILE_MISSING, MD_FILE_INVALID, MD_FILE_UNSUPPORTED, MD_FILE_IO };
typedef struct { int code, line; char key[40]; } MdFileError;
extern MdFilePreset md_custom_preset;
extern MdFileError md_runtime_error;
extern float md_preset_duration;
extern int md_output_width,md_output_height;
int md_eval_custom_waves(const MdFilePreset *p,float seconds,const MdSignal *signal,
    const short *right,const short *left,const float *spectrum_left,const float *spectrum_right,MdPresetState *state,
    MdWaveGeometry output[MD_CUSTOM_WAVES],MdFileError *error);
int md_eval_preset(const MdFilePreset *preset, float seconds, MdPreset *warp,
                   unsigned int *color, MdFileError *error);
int md_eval_preset_signal(const MdFilePreset *preset, float seconds, const MdSignal *signal,
                          MdPreset *warp, unsigned int *color, MdFileError *error);
int md_eval_preset_visual(const MdFilePreset *preset, float seconds, const MdSignal *signal,
                          MdPreset *warp, unsigned int *color, MdDecor *decor, MdFileError *error);
int md_eval_preset_state(const MdFilePreset *preset, float seconds, const MdSignal *signal,
                          MdPresetState *state, MdPreset *warp, unsigned int *color,
                          MdDecor *decor, MdFileError *error);
int md_eval_pixel_grid(const MdFilePreset *preset, const MdPreset *frame, float seconds,
                      const MdSignal *signal, MdPresetState *state,
                      MdPreset points[MD_GRID_POINTS], MdFileError *error);
/* Transactional: never change output on failure. Strict, bounded subset. */
int md_load_preset(const char *path, MdFilePreset *out, MdFileError *error);
#endif

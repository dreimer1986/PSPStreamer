#ifndef PSPSTREAMER_MILKDROP_PRESET_H
#define PSPSTREAMER_MILKDROP_PRESET_H
#include "milkdrop_warp.h"
#include "preset_math.h"
#include "milkdrop_signal.h"
#include "milkdrop_decor.h"
typedef struct {
    MdPreset warp; float red, green, blue; PmProgram program;
    int legacy, wave_mode, wrap;
    float gamma, wave_scale, wave_smoothing, wave_alpha;
    MdDecor decor;
    PmProgram init_program;
    PmSymbols symbols;
    PmProgram pixel_program;
} MdFilePreset;
/* Per-activation seeds. Frame q writes never accumulate into these seeds. */
typedef struct { int ready; float q[PM_Q_COUNT], user[PM_USER_COUNT], frame_q[PM_Q_COUNT]; } MdPresetState;
enum { MD_FILE_OK, MD_FILE_MISSING, MD_FILE_INVALID, MD_FILE_UNSUPPORTED, MD_FILE_IO };
typedef struct { int code, line; char key[40]; } MdFileError;
extern MdFilePreset md_custom_preset;
extern MdFileError md_runtime_error;
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
                      const MdSignal *signal, const MdPresetState *state,
                      MdPreset points[MD_GRID_POINTS], MdFileError *error);
/* Transactional: never change output on failure. Strict, bounded subset. */
int md_load_preset(const char *path, MdFilePreset *out, MdFileError *error);
#endif

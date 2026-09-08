#ifndef PSPSTREAMER_MILKDROP_PRESET_H
#define PSPSTREAMER_MILKDROP_PRESET_H
#include "milkdrop_warp.h"
#include "preset_math.h"
typedef struct { MdPreset warp; float red, green, blue; PmProgram program; } MdFilePreset;
enum { MD_FILE_OK, MD_FILE_MISSING, MD_FILE_INVALID, MD_FILE_UNSUPPORTED, MD_FILE_IO };
typedef struct { int code, line; char key[40]; } MdFileError;
extern MdFilePreset md_custom_preset;
extern MdFileError md_runtime_error;
int md_eval_preset(const MdFilePreset *preset, float seconds, MdPreset *warp,
                   unsigned int *color, MdFileError *error);
/* Transactional: never change output on failure. Strict, bounded subset. */
int md_load_preset(const char *path, MdFilePreset *out, MdFileError *error);
#endif

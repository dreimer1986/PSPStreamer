/* SPDX-License-Identifier: GPL-2.0-or-later
 * New bounded static/formula subset parser, not the original MilkDrop parser. */
#include "milkdrop_preset.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>

MdFilePreset md_custom_preset={.wave_mode=-1,.wrap=1,.gamma=1};
MdFileError md_runtime_error;
static const float low[] = {.8f, -.2f, -4, 0, .1f, .8f, 0, 0, 0};
static const float high[] = {1.2f, .2f, 4, 4, 8, 1, 1, 1, 1};
static char *md_trim(char *text) {
    char *end;
    while (isspace((unsigned char)*text)) text++;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) *--end = 0;
    return text;
}
static int md_file_error(MdFileError *e, int code, int line, const char *key) {
    e->code = code; e->line = line;
    snprintf(e->key, sizeof(e->key), "%.39s", key);
    return code;
}
int md_load_preset(const char *path, MdFilePreset *out, MdFileError *error) {
    static const char *keys[] = {"zoom", "rot", "warp", "fWarpAnimSpeed", "fWarpScale",
                                 "fDecay", "wave_r", "wave_g", "wave_b"};
    MdFilePreset next = {.warp = md_presets[0], .red = 1, .green = .6f, .blue = .2f,
        .wave_mode = -1, .wrap = 1, .gamma = 1, .wave_scale = 1, .wave_smoothing = .75f,
        .wave_alpha = 1};
    float wave_mode = -1, wrap = 1;
    struct { const char *key; float low, high; float *out; } extra[] = {
        {"dx",-1,1,&next.warp.dx}, {"dy",-1,1,&next.warp.dy},
        {"nWaveMode",0,0,&wave_mode}, {"bTexWrap",0,1,&wrap},
        {"fGammaAdj",1,4,&next.gamma}, {"fWaveScale",0,1,&next.wave_scale},
        {"fWaveSmoothing",0,1,&next.wave_smoothing}, {"fWaveAlpha",0,1,&next.wave_alpha},
        {"fRating",0,5,NULL}, {"fZoomExponent",1,1,NULL},
        {"cx",.5f,.5f,NULL}, {"cy",.5f,.5f,NULL}, {"sx",1,1,NULL}, {"sy",1,1,NULL},
        {"wave_x",.5f,.5f,NULL}, {"wave_y",.5f,.5f,NULL}, {"fWaveParam",0,0,NULL},
        {"fVideoEchoZoom",1,100,NULL}, {"fVideoEchoAlpha",0,0,NULL},
        {"nVideoEchoOrientation",0,0,NULL},
        {"bWaveDots",0,0,NULL}, {"bWaveThick",0,0,NULL}, {"bAdditiveWaves",0,0,NULL},
        {"bWaveScaleAtLeft",0,0,NULL}, {"bWaveWaveformAtLeft",0,0,NULL},
        {"bMaxContrast",0,0,NULL}, {"bRoundWarp",0,0,NULL}, {"bDarkenCenter",0,0,NULL},
        {"bRedBlueStereo",0,0,NULL}, {"bBrighten",0,0,NULL}, {"bDarken",0,0,NULL},
        {"bSolarize",0,0,NULL}, {"bInvert",0,0,NULL}, {"fShader",0,0,NULL},
        {"fModWaveAlphaStart",0,1,NULL}, {"fModWaveAlphaEnd",0,1,NULL},
        {"bModWaveAlphaByVolume",0,0,NULL},
        {"ob_size",0,1,NULL}, {"ob_r",0,1,NULL}, {"ob_g",0,1,NULL}, {"ob_b",0,1,NULL},
        {"ob_alpha",0,0,NULL}, {"ib_size",0,1,NULL}, {"ib_r",0,1,NULL},
        {"ib_g",0,1,NULL}, {"ib_b",0,1,NULL}, {"ib_alpha",0,0,NULL}
    };
    unsigned long long extra_seen = 0;
    int named = 0;
    float *values[] = {&next.warp.zoom, &next.warp.rotation, &next.warp.warp,
        &next.warp.warp_speed, &next.warp.warp_scale, &next.warp.decay,
        &next.red, &next.green, &next.blue};
    FILE *file = fopen(path, "rb");
    char line[256];
    int total = 0, number = 0, section = 0, result = MD_FILE_OK;
    unsigned int seen = 0;
    memset(error, 0, sizeof(*error));
    if (!file) return md_file_error(error, errno == ENOENT ? MD_FILE_MISSING : MD_FILE_IO, 0, "");
    for (;;) {
        int ch, used = 0, index;
        char *key, *value, *equal, *end;
        float parsed;
        number++;
        while ((ch = fgetc(file)) != EOF && ch != '\n') {
            if (++total > 16384 || used >= (int)sizeof(line)-1 || ch == 0) {
                result = md_file_error(error, MD_FILE_INVALID, number, "size/encoding");
                goto done;
            }
            line[used++] = (char)ch;
        }
        if (ch == '\n' && ++total > 16384) {
            result = md_file_error(error, MD_FILE_INVALID, number, "size");
            goto done;
        }
        if (ch == EOF && ferror(file)) { result = md_file_error(error, MD_FILE_IO, number, ""); goto done; }
        if (ch == EOF && !used) break;
        line[used] = 0; key = md_trim(line);
        /* ASCII fields, UTF-8 comments. Accept an optional UTF-8 BOM. */
        if (number == 1 && strlen(key) >= 3 && !memcmp(key, "\xef\xbb\xbf", 3)) key = md_trim(key+3);
        if (!*key || *key == ';' || *key == '#' || !strncmp(key, "//", 2)) continue;
        if (!strcmp(key, "[preset00]")) {
            if (section) { result = md_file_error(error, MD_FILE_INVALID, number, key); goto done; }
            section = 1; continue;
        }
        equal = strchr(key, '=');
        if (!equal) { result = md_file_error(error, MD_FILE_INVALID, number, key); goto done; }
        *equal = 0; key = md_trim(key); value = md_trim(equal+1);
        if (!strcmp(key,"presetName")) {
            if (named || !*value) { result=md_file_error(error,MD_FILE_INVALID,number,key); goto done; }
            named=1; section=1; next.legacy=1; continue;
        }
        if (!section) { result=md_file_error(error,MD_FILE_INVALID,number,key); goto done; }
        if (!strncmp(key, "per_frame_", 10)) {
            char expected[32];
            snprintf(expected, sizeof(expected), "per_frame_%d", next.program.lines+1);
            if (strcmp(key, expected)) {
                result = md_file_error(error, MD_FILE_INVALID, number, key); goto done;
            }
            int compiled = pm_compile(&next.program, value, number);
            if (compiled != PM_OK) {
                result = md_file_error(error, compiled == PM_UNSUPPORTED ?
                    MD_FILE_UNSUPPORTED : MD_FILE_INVALID, number, key); goto done;
            }
            continue;
        }
        for (index = 0; index < 9 && strcmp(key, keys[index]); index++) {}
        if (index == 9) {
            unsigned int k;
            for (k=0; k<sizeof(extra)/sizeof(extra[0]) && strcmp(key,extra[k].key); k++) {}
            if (k==sizeof(extra)/sizeof(extra[0])) {
                result=md_file_error(error,MD_FILE_UNSUPPORTED,number,key); goto done;
            }
            errno=0; parsed=strtof(value,&end);
            if (end==value || *md_trim(end) || errno==ERANGE || !isfinite(parsed) ||
                (extra_seen & (1ULL<<k))) {
                result=md_file_error(error,MD_FILE_INVALID,number,key); goto done;
            }
            if (parsed<extra[k].low || parsed>extra[k].high ||
                (!strcmp(key,"bTexWrap") && parsed!=0 && parsed!=1)) {
                result=md_file_error(error,MD_FILE_UNSUPPORTED,number,key); goto done;
            }
            if (extra[k].out) *extra[k].out=parsed;
            extra_seen |= 1ULL<<k; continue;
        }
        errno = 0; parsed = strtof(value, &end);
        if (end == value || *md_trim(end) || errno == ERANGE || !isfinite(parsed) ||
            parsed < low[index] || parsed > high[index] || (seen & (1U << index))) {
            result = md_file_error(error, MD_FILE_INVALID, number, key); goto done;
        }
        *values[index] = parsed; seen |= 1U << index;
    }
    next.wave_mode=(int)wave_mode; next.wrap=(int)wrap;
    if (next.wave_mode==0) next.legacy=1;
    if (!section || (!seen && !extra_seen && !next.program.count)) result = md_file_error(error, MD_FILE_INVALID, number, "empty");
done:
    if (fclose(file) && result == MD_FILE_OK)
        result = md_file_error(error, MD_FILE_IO, number, "");
    if (result == MD_FILE_OK) *out = next;
    return result;
}

int md_eval_preset(const MdFilePreset *p, float seconds, MdPreset *warp,
                   unsigned int *color, MdFileError *error) {
    return md_eval_preset_signal(p, seconds, NULL, warp, color, error);
}
int md_eval_preset_signal(const MdFilePreset *p, float seconds, const MdSignal *signal,
                          MdPreset *warp, unsigned int *color, MdFileError *error) {
    float v[PM_VALUES] = {p->warp.zoom, p->warp.rotation, p->warp.warp,
        p->warp.warp_speed, p->warp.warp_scale, p->warp.decay,
        p->red, p->green, p->blue, seconds};
    int line = 0;
    v[23]=p->warp.dx; v[24]=p->warp.dy;
    memset(error, 0, sizeof(*error));
    if (signal) for (int i = 0; i < MD_SIGNAL_COUNT; i++) {
        float value = signal->values[i];
        if (!isfinite(value) || value < 0 || (i < 7 && value > 1))
            return md_file_error(error, MD_FILE_INVALID, 0, "music input");
        v[10+i] = value;
    }
    if (!pm_execute(&p->program, v, &line))
        return md_file_error(error, MD_FILE_INVALID, line, "formula");
    for (int i = 0; i < 9; i++) {
        if (p->legacy && isfinite(v[i])) {
            if (i>=6) v[i]=fminf(1,fmaxf(0,v[i]));
            if (i==0 && v[i]>=.1f && v[i]<=64) continue;
        }
        if (!isfinite(v[i]) || v[i] < low[i] || v[i] > high[i]) {
            /* Last assignment to this output identifies the responsible line. */
            line = pm_assignment_line(&p->program, i);
            return md_file_error(error, MD_FILE_INVALID, line, "formula range");
        }
    }
    if (!isfinite(v[23]) || !isfinite(v[24]) || fabsf(v[23])>1 || fabsf(v[24])>1)
        return md_file_error(error,MD_FILE_INVALID,pm_assignment_line(&p->program,23),"translation");
    *warp = (MdPreset){v[0],v[1],v[2],v[3],v[4],v[5],v[23],v[24]};
    *color = 0xff000000U | (unsigned int)(v[6]*255) |
        ((unsigned int)(v[7]*255)<<8) | ((unsigned int)(v[8]*255)<<16);
    return MD_FILE_OK;
}

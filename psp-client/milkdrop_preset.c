/* SPDX-License-Identifier: GPL-2.0-or-later
 * New bounded static/formula subset parser, not the original MilkDrop parser. */
#include "milkdrop_preset.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <stddef.h>
_Static_assert(offsetof(MdShape,border_a)==21*sizeof(float),"shape field layout");
_Static_assert(offsetof(MdDecor,shapes)==MD_DECOR_VALUES*sizeof(float),"decor field layout");

MdFilePreset md_custom_preset={.wave_mode=-1,.wrap=1,.gamma=1,
    .decor={.wave_x=.5f,.wave_y=.5f,.echo_zoom=1}};
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
        .wave_alpha = 1, .decor={.wave_x=.5f,.wave_y=.5f,.echo_zoom=1,
            .wave_mod_start=.75f,.wave_mod_end=.95f}};
    for(int i=0;i<MD_SHAPES;i++) next.decor.shapes[i]=(MdShape){
        .sides=4,.x=.5f,.y=.5f,.rad=.1f,.tex_zoom=1,.r=1,.g=1,.b=1,.a=1,
        .r2=1,.g2=1,.b2=1,.border_r=1,.border_g=1,.border_b=1};
    unsigned int shape_seen[MD_SHAPES]={0};
    float wave_mode = -1, wrap = 1;
    struct { const char *key; float low, high; float *out; } extra[] = {
        {"dx",-1,1,&next.warp.dx}, {"dy",-1,1,&next.warp.dy},
        {"nWaveMode",0,0,&wave_mode}, {"bTexWrap",0,1,&wrap},
        {"fGammaAdj",1,4,&next.gamma}, {"fWaveScale",0,1,&next.wave_scale},
        {"fWaveSmoothing",0,1,&next.wave_smoothing}, {"fWaveAlpha",0,1,&next.wave_alpha},
        {"fRating",0,5,NULL}, {"fZoomExponent",.5f,2,&next.warp.zoomexp},
        {"cx",0,1,&next.warp.cx}, {"cy",0,1,&next.warp.cy},
        {"sx",.25f,4,&next.warp.sx}, {"sy",.25f,4,&next.warp.sy},
        {"wave_x",0,1,&next.decor.wave_x}, {"wave_y",0,1,&next.decor.wave_y},
        {"fWaveParam",-1,1,&next.decor.wave_param},
        {"fVideoEchoZoom",1,100,&next.decor.echo_zoom},
        {"fVideoEchoAlpha",0,1,&next.decor.echo_alpha},
        {"nVideoEchoOrientation",0,3,&next.decor.echo_orient},
        {"bWaveDots",0,1,&next.decor.wave_dots}, {"bWaveThick",0,1,&next.decor.wave_thick},
        {"bAdditiveWaves",0,1,&next.decor.wave_additive},
        {"bMaximizeWaveColor",0,1,&next.decor.wave_brighten},
        {"bWaveScaleAtLeft",0,0,NULL}, {"bWaveWaveformAtLeft",0,0,NULL},
        {"bMaxContrast",0,0,NULL}, {"bRoundWarp",0,0,NULL}, {"bDarkenCenter",0,0,NULL},
        {"bRedBlueStereo",0,0,NULL}, {"bBrighten",0,0,NULL}, {"bDarken",0,0,NULL},
        {"bSolarize",0,0,NULL}, {"bInvert",0,0,NULL}, {"fShader",0,0,NULL},
        {"fModWaveAlphaStart",0,4,&next.decor.wave_mod_start},
        {"fModWaveAlphaEnd",0,4,&next.decor.wave_mod_end},
        {"bModWaveAlphaByVolume",0,1,&next.decor.wave_mod_alpha},
        {"ob_size",0,.5f,&next.decor.outer.size}, {"ob_r",0,1,&next.decor.outer.r},
        {"ob_g",0,1,&next.decor.outer.g}, {"ob_b",0,1,&next.decor.outer.b},
        {"ob_alpha",0,1,&next.decor.outer.a}, {"ib_size",0,.5f,&next.decor.inner.size},
        {"ib_r",0,1,&next.decor.inner.r}, {"ib_g",0,1,&next.decor.inner.g},
        {"ib_b",0,1,&next.decor.inner.b}, {"ib_alpha",0,1,&next.decor.inner.a}
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
        if (!strncmp(key,"shapecode_",10)) {
            int slot=key[10]-'0';
            static const char *names[]={"enabled","sides","additive","textured","x","y",
                "rad","ang","tex_ang","tex_zoom","r","g","b","a","r2","g2","b2","a2",
                "border_r","border_g","border_b","border_a","thickOutline"};
            if(strlen(key)<13 || slot<0 || slot>=MD_SHAPES || key[11]!='_') {
                result=md_file_error(error,MD_FILE_UNSUPPORTED,number,key); goto done;
            }
            int k; for(k=0;k<23 && strcmp(key+12,names[k]);k++) {}
            if(k==23) { result=md_file_error(error,MD_FILE_UNSUPPORTED,number,key); goto done; }
            errno=0; parsed=strtof(value,&end);
            float lo=0,hi=1;
            if(k==1) {lo=3;hi=MD_SHAPE_SIDES;}
            if(k==7 || k==8) {lo=-100;hi=100;}
            if(k==9) {lo=.1f;hi=10;}
            if(k==22) hi=0; /* Thick custom-shape outlines need a separate list budget. */
            if(end==value || *md_trim(end) || errno==ERANGE || !isfinite(parsed) ||
                (shape_seen[slot]&(1U<<k))) {
                result=md_file_error(error,MD_FILE_INVALID,number,key); goto done;
            }
            if(parsed<lo || parsed>hi || (k<4 && parsed!=floorf(parsed))) {
                result=md_file_error(error,MD_FILE_UNSUPPORTED,number,key); goto done;
            }
            /* All MdShape members are floats; memcpy avoids cross-member pointer arithmetic. */
            if(k<22) memcpy((char *)&next.decor.shapes[slot]+k*sizeof(float),&parsed,sizeof(parsed));
            shape_seen[slot]|=1U<<k; continue;
        }
        if (!strncmp(key, "per_frame_", 10)) {
            char expected[32];
            int init=!strncmp(key,"per_frame_init_",15);
            PmProgram *program=init?&next.init_program:&next.program;
            snprintf(expected, sizeof(expected), init?"per_frame_init_%d":"per_frame_%d", program->lines+1);
            if (strcmp(key, expected)) {
                result = md_file_error(error, MD_FILE_INVALID, number, key); goto done;
            }
            int compiled = pm_compile_symbols(program, value, number, &next.symbols);
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
                ((key[0]=='b' || !strcmp(key,"nVideoEchoOrientation")) && parsed!=floorf(parsed))) {
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
    if(next.decor.wave_mod_alpha && next.decor.wave_mod_end<=next.decor.wave_mod_start)
        result=md_file_error(error,MD_FILE_INVALID,number,"wave alpha range");
    if (next.wave_mode==0) next.legacy=1;
    if (!section || (!seen && !extra_seen && !next.program.count && !next.init_program.count &&
        !(shape_seen[0]|shape_seen[1]|shape_seen[2]|shape_seen[3]))) result = md_file_error(error, MD_FILE_INVALID, number, "empty");
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
    MdDecor decor;
    return md_eval_preset_visual(p,seconds,signal,warp,color,&decor,error);
}
int md_eval_preset_visual(const MdFilePreset *p, float seconds, const MdSignal *signal,
                          MdPreset *warp, unsigned int *color, MdDecor *decor, MdFileError *error) {
    /* Stateless convenience API; playback uses an explicit activation state. */
    MdPresetState state={0};
    return md_eval_preset_state(p,seconds,signal,&state,warp,color,decor,error);
}
int md_eval_preset_state(const MdFilePreset *p, float seconds, const MdSignal *signal,
                          MdPresetState *state, MdPreset *warp, unsigned int *color,
                          MdDecor *decor, MdFileError *error) {
    float v[PM_VALUES] = {p->warp.zoom, p->warp.rotation, p->warp.warp,
        p->warp.warp_speed, p->warp.warp_scale, p->warp.decay,
        p->red, p->green, p->blue, seconds};
    int line = 0;
    v[23]=p->warp.dx; v[24]=p->warp.dy;
    v[25]=p->warp.cx; v[26]=p->warp.cy; v[27]=p->warp.sx; v[28]=p->warp.sy; v[29]=p->warp.zoomexp;
    memcpy(v+30,&p->decor,MD_DECOR_VALUES*sizeof(float));
    v[53]=p->gamma; v[54]=p->wave_alpha;
    memset(error, 0, sizeof(*error));
    if (signal) for (int i = 0; i < MD_SIGNAL_COUNT; i++) {
        float value = signal->values[i];
        if (!isfinite(value) || value < 0 || (i < 7 && value > 1))
            return md_file_error(error, MD_FILE_INVALID, 0, "music input");
        v[10+i] = value;
    }
    MdPresetState next=*state;
    if (!next.ready) {
        float initial[PM_VALUES];
        memcpy(initial,v,sizeof(initial));
        if (!pm_execute(&p->init_program,initial,&line))
            return md_file_error(error,MD_FILE_INVALID,line,"init formula");
        memcpy(next.q,initial+PM_Q_BASE,sizeof(next.q));
        memcpy(next.user,initial+PM_USER_BASE,sizeof(next.user));
        next.ready=1;
    }
    /* Reference LoadPerFrameEvallibVars restores q_values_after_init_code
     * before every frame. Ordinary output fields also restart from static. */
    memcpy(v+PM_Q_BASE,next.q,sizeof(next.q));
    memcpy(v+PM_USER_BASE,next.user,sizeof(next.user));
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
    for(int i=25;i<30;i++) {
        float lo=i<27?0:i<29?.25f:.5f, hi=i<27?1:i<29?4:2;
        if(!isfinite(v[i]) || v[i]<lo || v[i]>hi)
            return md_file_error(error,MD_FILE_INVALID,pm_assignment_line(&p->program,i),"transform");
    }
    for(int i=0;i<MD_DECOR_VALUES;i++) {
        float lo=i==2?-1:0, hi=(i==8 || i==9 || i==23)?4:i==10?100:i==12?3:(i==13 || i==18)?.5f:1;
        if(i==10 || i==23) lo=1;
        float value=v[30+i];
        if(!isfinite(value) || value<lo || value>hi ||
           (((i>=3 && i<=7) || i==12) && value!=floorf(value)))
            return md_file_error(error,MD_FILE_INVALID,pm_assignment_line(&p->program,30+i),"visual field");
    }
    if(v[37] && v[39]<=v[38])
        return md_file_error(error,MD_FILE_INVALID,0,"wave alpha range");
    *decor=p->decor;
    memcpy(decor,v+30,MD_DECOR_VALUES*sizeof(float));
    *warp = (MdPreset){v[0],v[1],v[2],v[3],v[4],v[5],v[23],v[24],v[25],v[26],v[27],v[28],v[29]};
    *color = 0xff000000U | (unsigned int)(v[6]*255) |
        ((unsigned int)(v[7]*255)<<8) | ((unsigned int)(v[8]*255)<<16);
    memcpy(next.user,v+PM_USER_BASE,sizeof(next.user));
    *state=next;
    return MD_FILE_OK;
}

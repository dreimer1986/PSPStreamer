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
_Static_assert(offsetof(MdShape,thick_outline)==22*sizeof(float),"shape outline field layout");
_Static_assert(offsetof(MdDecor,shapes)==MD_DECOR_VALUES*sizeof(float),"decor field layout");
_Static_assert(offsetof(MdCustomWave,a)==12*sizeof(float),"custom wave field layout");

MdFilePreset md_custom_preset={.wave_mode=-1,.wrap=1,.gamma=1,
    .decor={.wave_x=.5f,.wave_y=.5f,.echo_zoom=1}};
MdFileError md_runtime_error;
float md_preset_duration=60;
int md_output_width=480,md_output_height=272;
static void md_inputs(float *v) {
    float w=md_output_width>0?md_output_width:480,h=md_output_height>0?md_output_height:272;
    v[PM_INPUT_BASE]=MD_GRID;v[PM_INPUT_BASE+1]=MD_GRID;
    v[PM_INPUT_BASE+2]=w;v[PM_INPUT_BASE+3]=h;
    v[PM_INPUT_BASE+4]=w>=h?1:h/w;v[PM_INPUT_BASE+5]=h>=w?1:w/h;
}
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
    MdFilePreset *working=calloc(1,sizeof(*working));
    if(!working) return md_file_error(error,MD_FILE_IO,0,"preset allocation");
    /* Large compiled programs belong on the heap, not the PSP thread stack. */
#define next (*working)
    next.warp=md_presets[0];next.red=1;next.green=.6f;next.blue=.2f;
    next.wave_mode=-1;next.wrap=1;next.gamma=1;next.wave_scale=1;next.wave_smoothing=.75f;
    next.wave_alpha=1;
    const float initial_motion[9]={0,1,1,1,12,9,0,0,1};
    memcpy(next.motion,initial_motion,sizeof(initial_motion));
    next.decor=(MdDecor){.wave_x=.5f,.wave_y=.5f,.echo_zoom=1,.wave_mod_start=.75f,.wave_mod_end=.95f};
    for(int i=0;i<MD_SHAPES;i++) next.decor.shapes[i]=(MdShape){
        .sides=4,.x=.5f,.y=.5f,.rad=.1f,.tex_zoom=1,.r=1,.g=1,.b=1,.a=1,
        .r2=1,.g2=1,.b2=1,.border_r=1,.border_g=1,.border_b=1};
    unsigned int shape_seen[MD_SHAPES]={0};
    for(int i=0;i<MD_SHAPES;i++) next.shape_instances[i]=1;
    unsigned int wave_seen[MD_CUSTOM_WAVES]={0};
    for(int i=0;i<MD_CUSTOM_WAVES;i++) next.waves[i]=(MdCustomWave){.samples=64,.scaling=1,.smoothing=.5f,.r=1,.g=1,.b=1,.a=1};
    float wave_mode = -1, wrap = 1;
    struct { const char *key; float low, high; float *out; } extra[] = {
        {"mv_a",0,1,&next.motion[0]}, {"mv_r",0,1,&next.motion[1]},
        {"mv_g",0,1,&next.motion[2]}, {"mv_b",0,1,&next.motion[3]},
        {"mv_x",0,16,&next.motion[4]}, {"mv_y",0,12,&next.motion[5]},
        {"mv_dx",-1,1,&next.motion[6]}, {"mv_dy",-1,1,&next.motion[7]},
        {"mv_l",0,10,&next.motion[8]},
        {"dx",-1,1,&next.warp.dx}, {"dy",-1,1,&next.warp.dy},
        {"nWaveMode",0,8,&wave_mode}, {"bTexWrap",0,1,&wrap},
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
        {"bMaxContrast",0,0,NULL}, {"bRoundWarp",0,0,NULL}, {"bDarkenCenter",0,1,&next.effects[0]},
        {"bRedBlueStereo",0,0,NULL}, {"bBrighten",0,1,&next.effects[1]}, {"bDarken",0,1,&next.effects[2]},
        {"bSolarize",0,1,&next.effects[3]}, {"bInvert",0,1,&next.effects[4]}, {"fShader",0,0,NULL},
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
    char line[2048];
    int total = 0, number = 0, section = 0, result = MD_FILE_OK;
    unsigned int seen = 0;
    memset(error, 0, sizeof(*error));
    if (!file) {int code=errno==ENOENT?MD_FILE_MISSING:MD_FILE_IO;free(working);return md_file_error(error,code,0,"");}
    for (;;) {
        int ch, used = 0, index;
        char *key, *value, *equal, *end;
        float parsed;
        number++;
        while ((ch = fgetc(file)) != EOF && ch != '\n') {
            if (++total > 65536 || used >= (int)sizeof(line)-1 || ch == 0) {
                result = md_file_error(error, MD_FILE_INVALID, number, "size/encoding");
                goto done;
            }
            line[used++] = (char)ch;
        }
        if (ch == '\n' && ++total > 65536) {
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
        if(!strncmp(key,"wavecode_",9)) {
            int slot=key[9]-'0';
            static const char *names[]={"enabled","samples","sep","bSpectrum","bUseDots","bDrawThick","bAdditive","scaling","smoothing","r","g","b","a"};
            if(strlen(key)<12 || slot<0 || slot>=MD_CUSTOM_WAVES || key[10]!='_') { result=md_file_error(error,MD_FILE_INVALID,number,key); goto done; }
            int k; for(k=0;k<13 && strcmp(key+11,names[k]);k++) {}
            if(k==13) {result=md_file_error(error,MD_FILE_UNSUPPORTED,number,key); goto done;}
            errno=0; parsed=strtof(value,&end);
            float lo=k==1?2:0, hi=k==1?MD_CUSTOM_POINTS:k==2?128:k==7?4:1;
            if(end==value || *md_trim(end) || errno==ERANGE || !isfinite(parsed) || (wave_seen[slot]&(1U<<k))) {result=md_file_error(error,MD_FILE_INVALID,number,key); goto done;}
            if(parsed<lo || parsed>hi || (k<7 && parsed!=floorf(parsed))) {result=md_file_error(error,MD_FILE_UNSUPPORTED,number,key); goto done;}
            memcpy((char *)&next.waves[slot]+k*sizeof(float),&parsed,sizeof(parsed));
            wave_seen[slot]|=1U<<k; continue;
        }
        if(!strncmp(key,"wave_",5) && isdigit((unsigned char)key[5])) {
            int slot=key[5]-'0';
            if(strlen(key)<8 || slot<0 || slot>=MD_CUSTOM_WAVES || key[6]!='_') {result=md_file_error(error,MD_FILE_INVALID,number,key); goto done;}
            MdCustomWave *w=&next.waves[slot];
            int init=!strncmp(key+7,"init",4),point=!strncmp(key+7,"per_point",9);
            PmProgram *program=init?&w->init:point?&w->point:&w->frame;
            char expected[40]; snprintf(expected,sizeof(expected),init?"wave_%d_init%d":point?"wave_%d_per_point%d":"wave_%d_per_frame%d",slot,program->lines+1);
            if(strcmp(key,expected)) {result=md_file_error(error,MD_FILE_INVALID,number,key); goto done;}
            int code=pm_compile_wave(program,value,number,point?&w->point_symbols:&w->symbols,point);
            if(code!=PM_OK) {result=md_file_error(error,code==PM_UNSUPPORTED?MD_FILE_UNSUPPORTED:MD_FILE_INVALID,number,key); goto done;}
            wave_seen[slot]|=1U<<31; continue;
        }
        if (!strncmp(key,"shape_",6)) {
            int slot=key[6]-'0';
            if(strlen(key)<9 || slot<0 || slot>=MD_SHAPES || key[7]!='_') {
                result=md_file_error(error,MD_FILE_UNSUPPORTED,number,key); goto done;
            }
            MdShapeProgram *shape=&next.shape_program[slot];
            int init=!strncmp(key+8,"init",4);
            PmProgram *program=init?&shape->init:&shape->frame;
            char expected[40];
            snprintf(expected,sizeof(expected),init?"shape_%d_init%d":"shape_%d_per_frame%d",slot,program->lines+1);
            if(strcmp(key,expected)) { result=md_file_error(error,MD_FILE_INVALID,number,key); goto done; }
            int code=pm_compile_shape(program,value,number,&shape->symbols);
            if(code!=PM_OK) { result=md_file_error(error,code==PM_UNSUPPORTED?MD_FILE_UNSUPPORTED:MD_FILE_INVALID,number,key); goto done; }
            shape_seen[slot]|=1U<<31;
            continue;
        }
        if (!strncmp(key,"shapecode_",10)) {
            int slot=key[10]-'0';
            static const char *names[]={"enabled","sides","additive","textured","x","y",
                "rad","ang","tex_ang","tex_zoom","r","g","b","a","r2","g2","b2","a2",
                "border_r","border_g","border_b","border_a","thickOutline"};
            if(strlen(key)<13 || slot<0 || slot>=MD_SHAPES || key[11]!='_') {
                result=md_file_error(error,MD_FILE_UNSUPPORTED,number,key); goto done;
            }
            if(!strcmp(key+12,"num_inst")) {
                errno=0; parsed=strtof(value,&end);
                if(end==value || *md_trim(end) || errno || !isfinite(parsed) || parsed<1 || parsed>MD_SHAPE_INSTANCES || parsed!=floorf(parsed) || (shape_seen[slot]&(1U<<30))) {result=md_file_error(error,MD_FILE_INVALID,number,key);goto done;}
                next.shape_instances[slot]=(int)parsed;shape_seen[slot]|=1U<<30;continue;
            }
            int k; for(k=0;k<23 && strcmp(key+12,names[k]);k++) {}
            if(k==23) { result=md_file_error(error,MD_FILE_UNSUPPORTED,number,key); goto done; }
            errno=0; parsed=strtof(value,&end);
            float lo=0,hi=1;
            if(k==1) {lo=3;hi=MD_SHAPE_SIDES;}
            if(k==7 || k==8) {lo=-100;hi=100;}
            if(k==9) {lo=.1f;hi=10;}
            if(end==value || *md_trim(end) || errno==ERANGE || !isfinite(parsed) ||
                (shape_seen[slot]&(1U<<k))) {
                result=md_file_error(error,MD_FILE_INVALID,number,key); goto done;
            }
            if(parsed<lo || parsed>hi || ((k<4 || k==22) && parsed!=floorf(parsed))) {
                result=md_file_error(error,MD_FILE_UNSUPPORTED,number,key); goto done;
            }
            /* All MdShape members are floats; memcpy avoids cross-member pointer arithmetic. */
            memcpy((char *)&next.decor.shapes[slot]+k*sizeof(float),&parsed,sizeof(parsed));
            shape_seen[slot]|=1U<<k; continue;
        }
        if (!strncmp(key, "per_frame_", 10) || !strncmp(key,"per_pixel_",10)) {
            char expected[32];
            int init=!strncmp(key,"per_frame_init_",15);
            int pixel=!strncmp(key,"per_pixel_",10);
            PmProgram *program=pixel?&next.pixel_program:init?&next.init_program:&next.program;
            snprintf(expected, sizeof(expected), pixel?"per_pixel_%d":init?"per_frame_init_%d":"per_frame_%d", program->lines+1);
            if (strcmp(key, expected)) {
                result = md_file_error(error, MD_FILE_INVALID, number, key); goto done;
            }
            int compiled = pixel?pm_compile_pixel_symbols(program,value,number,&next.pixel_symbols):
                pm_compile_symbols(program, value, number, &next.symbols);
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
                (!strcmp(key,"nWaveMode") && parsed!=floorf(parsed)) ||
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
    if (next.wave_mode>=0) next.legacy=1;
    if (!section || (!seen && !extra_seen && !next.program.count && !next.init_program.count && !next.pixel_program.count &&
        !(shape_seen[0]|shape_seen[1]|shape_seen[2]|shape_seen[3]) &&
        !(wave_seen[0]|wave_seen[1]|wave_seen[2]|wave_seen[3]))) result = md_file_error(error, MD_FILE_INVALID, number, "empty");
done:
    if (fclose(file) && result == MD_FILE_OK)
        result = md_file_error(error, MD_FILE_IO, number, "");
    if (result == MD_FILE_OK) *out = next;
    free(working);
#undef next
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
    v[PM_DYNAMIC_BASE]=(float)p->wave_mode;
    memcpy(v+PM_DYNAMIC_BASE+1,p->motion,sizeof(p->motion));
    memcpy(v+PM_EFFECT_BASE,p->effects,sizeof(p->effects));
    v[PM_ENGINE_BASE+2]=fminf(1,fmaxf(0,seconds/(md_preset_duration>0?md_preset_duration:60)));
    memset(error, 0, sizeof(*error));
    if (signal) for (int i = 0; i < MD_SIGNAL_COUNT; i++) {
        float value = signal->values[i];
        if (!isfinite(value) || value < 0 || (i < 7 && value > 1))
            return md_file_error(error, MD_FILE_INVALID, 0, "music input");
        v[10+i] = value;
    }
    MdPresetState next=*state;
    pm_begin_frame();md_inputs(v);v[PM_MONITOR]=next.monitor;
    v[PM_WRAP]=(float)p->wrap;
    float dt=seconds-next.last_seconds;
    /* Presets commonly divide by fps. Seed the first visual frame from the
     * renderer's 50-ms minimum interval; subsequent frames use measured time.
     * A same-timestamp layout redraw retains the last valid rate. */
    if(next.frames && dt>0) next.fps=1.0f/dt;
    else if(!isfinite(next.fps) || next.fps<=0) next.fps=20;
    v[PM_META_BASE]=(float)next.frames;
    v[PM_META_BASE+1]=next.fps;
    if (!next.ready) {
        float initial[PM_VALUES];
        memcpy(initial,v,sizeof(initial));
        if (!pm_execute_runtime(&p->init_program,initial,&line,&next.runtime))
            return md_file_error(error,MD_FILE_INVALID,line,"init formula");
        memcpy(next.q,initial+PM_Q_BASE,sizeof(next.q));
        memcpy(next.user,initial+PM_USER_BASE,sizeof(next.user));
        next.monitor=initial[PM_MONITOR];v[PM_MONITOR]=next.monitor;
        next.ready=1;
    }
    /* Reference LoadPerFrameEvallibVars restores q_values_after_init_code
     * before every frame. Ordinary output fields also restart from static. */
    memcpy(v+PM_Q_BASE,next.q,sizeof(next.q));
    memcpy(v+PM_USER_BASE,next.user,sizeof(next.user));
    if (!pm_execute_runtime(&p->program, v, &line,&next.runtime))
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
    float mode=v[PM_DYNAMIC_BASE];
    int mode_line=pm_assignment_line(&p->program,PM_DYNAMIC_BASE);
    if(!isfinite(mode) || mode!=floorf(mode) || mode>8 || mode<(mode_line?0:-1))
        return md_file_error(error,MD_FILE_INVALID,mode_line,"wave mode");
    for(int i=0;i<9;i++) {
        float lo=(i==6 || i==7)?-1:0, hi=i==4?16:i==5?12:i==8?10:1;
        float value=v[PM_DYNAMIC_BASE+1+i];
        if(!isfinite(value) || value<lo || value>hi)
            return md_file_error(error,MD_FILE_INVALID,pm_assignment_line(&p->program,PM_DYNAMIC_BASE+1+i),"motion vectors");
    }
    next.wave_mode=(int)mode;
    for(int i=0;i<5;i++) {
        float value=v[PM_EFFECT_BASE+i];
        if(!isfinite(value) || (value!=0 && value!=1)) return md_file_error(error,MD_FILE_INVALID,pm_assignment_line(&p->program,PM_EFFECT_BASE+i),"image effect");
        next.effects[i]=value;
    }
    memcpy(next.motion,v+PM_DYNAMIC_BASE+1,sizeof(next.motion));
    next.wrap=fabsf(v[PM_WRAP])>=.00001f;
    MdDecor evaluated=p->decor;
    for(int slot=0;slot<MD_SHAPES;slot++) {
        const MdShapeProgram *program=&p->shape_program[slot];
        if(!p->decor.shapes[slot].enabled) continue;
        int instances=p->shape_instances[slot]>0?p->shape_instances[slot]:1;
        if(instances>MD_SHAPE_INSTANCES) return md_file_error(error,MD_FILE_INVALID,0,"shape instances");
        for(int instance=0;instance<instances;instance++) {
        MdShapeState *local=&next.shape[slot];
        float sv[PM_VALUES]={0};
        md_inputs(sv);
        memcpy(sv+9,v+9,14*sizeof(float));
        memcpy(sv+PM_META_BASE,v+PM_META_BASE,2*sizeof(float));
        sv[PM_ENGINE_BASE]=(float)instance;sv[PM_ENGINE_BASE+1]=(float)instances;sv[PM_ENGINE_BASE+2]=v[PM_ENGINE_BASE+2];
        memcpy(sv+PM_Q_BASE,v+PM_Q_BASE,PM_Q_COUNT*sizeof(float));
        memcpy(sv+PM_SHAPE_BASE,&p->decor.shapes[slot],sizeof(MdShape));
        memcpy(sv+PM_USER_BASE,local->user,sizeof(local->user));
        if(!local->ready) {
            memcpy(sv+PM_Q_BASE,next.q,sizeof(next.q));
            if(!pm_execute_runtime(&program->init,sv,&line,&local->runtime)) return md_file_error(error,MD_FILE_INVALID,line,"shape init");
            memcpy(local->t,sv+PM_T_BASE,sizeof(local->t));
            memcpy(local->user,sv+PM_USER_BASE,sizeof(local->user));
            local->ready=1;
        }
        /* Shape q reads the current preset frame; t restores init seeds.
         * Shape outputs restart from static values; named locals persist. */
        memcpy(sv+PM_Q_BASE,v+PM_Q_BASE,PM_Q_COUNT*sizeof(float));
        memcpy(sv+PM_T_BASE,local->t,sizeof(local->t));
        memcpy(sv+PM_SHAPE_BASE,&p->decor.shapes[slot],sizeof(MdShape));
        if(!pm_execute_runtime(&program->frame,sv,&line,&local->runtime)) return md_file_error(error,MD_FILE_INVALID,line,"shape frame");
        for(int k=0;k<23;k++) {
            float value=sv[PM_SHAPE_BASE+k],lo=0,hi=1;
            if(k==1) {lo=3;hi=MD_SHAPE_SIDES;}
            if(k==7 || k==8) {lo=-100;hi=100;}
            if(k==9) {lo=.1f;hi=10;}
            if(!isfinite(value) || value<lo || value>hi || ((k<4 || k==22) && value!=floorf(value)))
                return md_file_error(error,MD_FILE_INVALID,pm_assignment_line(&program->frame,PM_SHAPE_BASE+k),"shape range");
        }
        int output=instance?MD_SHAPES+slot*(MD_SHAPE_INSTANCES-1)+instance-1:slot;
        memcpy(&evaluated.shapes[output],sv+PM_SHAPE_BASE,sizeof(MdShape));
        memcpy(local->user,sv+PM_USER_BASE,sizeof(local->user));
        }
    }
    *decor=evaluated;
    memcpy(decor,v+30,MD_DECOR_VALUES*sizeof(float));
    *warp = (MdPreset){v[0],v[1],v[2],v[3],v[4],v[5],v[23],v[24],v[25],v[26],v[27],v[28],v[29]};
    *color = 0xff000000U | (unsigned int)(v[6]*255) |
        ((unsigned int)(v[7]*255)<<8) | ((unsigned int)(v[8]*255)<<16);
    memcpy(next.user,v+PM_USER_BASE,sizeof(next.user));
    memcpy(next.frame_q,v+PM_Q_BASE,sizeof(next.frame_q));
    next.monitor=v[PM_MONITOR];
    next.frames++; next.last_seconds=seconds;
    *state=next;
    return MD_FILE_OK;
}

int md_eval_custom_waves(const MdFilePreset *p,float seconds,const MdSignal *signal,
    const short *right,const short *left,const float *spectrum_left,const float *spectrum_right,MdPresetState *state,
    MdWaveGeometry output[MD_CUSTOM_WAVES],MdFileError *error) {
    MdPresetState next=*state;
    MdWaveGeometry geometry[MD_CUSTOM_WAVES]={0};
    int line=0;
    for(int slot=0;slot<MD_CUSTOM_WAVES;slot++) {
        const MdCustomWave *w=&p->waves[slot];
        if(!w->enabled) continue;
        if(w->spectrum && (!spectrum_left || !spectrum_right)) return md_file_error(error,MD_FILE_INVALID,0,"spectrum unavailable");
        MdWaveState *ws=&next.waves[slot];
        float v[PM_VALUES]={0};
        md_inputs(v);
        v[9]=seconds;
        v[PM_ENGINE_BASE+2]=fminf(1,fmaxf(0,seconds/(md_preset_duration>0?md_preset_duration:60)));
        if(signal) memcpy(v+10,signal->values,MD_SIGNAL_COUNT*sizeof(float));
        v[PM_META_BASE]=next.frames?(float)(next.frames-1):0; v[PM_META_BASE+1]=next.fps;
        memcpy(v+PM_Q_BASE,next.q,sizeof(next.q));
        memcpy(v+PM_USER_BASE,ws->frame.user,sizeof(ws->frame.user));
        memcpy(v+PM_SHAPE_BASE+10,&w->r,4*sizeof(float)); v[PM_WAVE_BASE]=w->samples;
        if(!ws->frame.ready) {
            if(!pm_execute_runtime(&w->init,v,&line,&ws->frame.runtime)) return md_file_error(error,MD_FILE_INVALID,line,"wave init");
            memcpy(ws->frame.t,v+PM_T_BASE,sizeof(ws->frame.t)); ws->frame.ready=1;
        }
        memcpy(v+PM_T_BASE,ws->frame.t,sizeof(ws->frame.t));
        memcpy(v+PM_Q_BASE,next.frame_q,sizeof(next.frame_q));
        memcpy(v+PM_SHAPE_BASE+10,&w->r,4*sizeof(float)); v[PM_WAVE_BASE]=w->samples;
        if(!pm_execute_runtime(&w->frame,v,&line,&ws->frame.runtime)) return md_file_error(error,MD_FILE_INVALID,line,"wave frame");
        float n=v[PM_WAVE_BASE];
        if(!isfinite(n) || n<2 || n>MD_CUSTOM_POINTS || n!=floorf(n)) return md_file_error(error,MD_FILE_INVALID,pm_assignment_line(&w->frame,PM_WAVE_BASE),"wave samples");
        memcpy(ws->frame.user,v+PM_USER_BASE,sizeof(ws->frame.user));
        float colors[4]; memcpy(colors,v+PM_SHAPE_BASE+10,sizeof(colors));
        memcpy(v+PM_USER_BASE,ws->point_user,sizeof(ws->point_user));
        int count=(int)n,offset=(576-count)/2,sep=(int)w->sep;
        if(!w->spectrum && count+sep>576) return md_file_error(error,MD_FILE_INVALID,0,"wave sample separation");
        float a[MD_CUSTOM_POINTS],b[MD_CUSTOM_POINTS];
        float mix=sqrtf(w->smoothing*.98f),gain=w->scaling*p->wave_scale/32768.0f;
        for(int i=0;i<count;i++) {
            if(w->spectrum) {
                int bin=i*(512-sep)/count;
                float sg=w->scaling*p->wave_scale;
                a[i]=spectrum_left[bin]*sg; b[i]=spectrum_right[bin]*sg;
            } else {a[i]=left[offset+i-sep/2]*gain; b[i]=right[offset+i+sep/2]*gain;}
            if(i) {a[i]=a[i]*(1-mix)+a[i-1]*mix; b[i]=b[i]*(1-mix)+b[i-1]*mix;}
        }
        for(int i=count-2;i>=0;i--) {a[i]=a[i]*(1-mix)+a[i+1]*mix; b[i]=b[i]*(1-mix)+b[i+1]*mix;}
        for(int i=0;i<count;i++) {
            v[PM_WAVE_BASE+1]=(float)i/(count-1); v[PM_WAVE_BASE+2]=a[i]; v[PM_WAVE_BASE+3]=b[i];
            v[PM_SHAPE_BASE+4]=.5f+a[i]; v[PM_SHAPE_BASE+5]=.5f+b[i];
            memcpy(v+PM_SHAPE_BASE+10,colors,sizeof(colors));
            if(!pm_execute_runtime(&w->point,v,&line,&ws->point_runtime)) return md_file_error(error,MD_FILE_INVALID,line,"wave point");
            for(int k=0;k<4;k++) if(!isfinite(v[PM_SHAPE_BASE+10+k]) || v[PM_SHAPE_BASE+10+k]<0 || v[PM_SHAPE_BASE+10+k]>1)
                return md_file_error(error,MD_FILE_INVALID,line,"wave color");
            float x=v[PM_SHAPE_BASE+4],y=v[PM_SHAPE_BASE+5];
            if(!isfinite(x) || !isfinite(y) || x<0 || x>1 || y<0 || y>1)
                return md_file_error(error,MD_FILE_INVALID,line,"wave position");
            geometry[slot].vertices[i]=(MdVertex){.x=x*256,.y=y*256,
                .color=md_rgba(v[PM_SHAPE_BASE+10],v[PM_SHAPE_BASE+11],v[PM_SHAPE_BASE+12],v[PM_SHAPE_BASE+13])};
        }
        memcpy(ws->point_user,v+PM_USER_BASE,sizeof(ws->point_user));
        geometry[slot].count=count;
    }
    memcpy(output,geometry,sizeof(geometry)); *state=next;
    return MD_FILE_OK;
}

int md_eval_pixel_grid(const MdFilePreset *p, const MdPreset *frame, float seconds,
                      const MdSignal *signal, MdPresetState *state,
                      MdPreset points[MD_GRID_POINTS], MdFileError *error) {
    MdPreset next[MD_GRID_POINTS];
    PmRuntime runtime=state->pixel_runtime;
    float users[PM_USER_COUNT],q[PM_Q_COUNT];
    memcpy(users,state->pixel_user,sizeof(users));memcpy(q,state->frame_q,sizeof(q));
    memset(error,0,sizeof(*error));
    if(p->pixel_program.count<0 || p->pixel_program.count>PM_PIXEL_OPS)
        return md_file_error(error,MD_FILE_INVALID,0,"pixel budget");
    for(int y=0;y<=MD_GRID;y++) for(int x=0;x<=MD_GRID;x++) {
        float v[PM_VALUES]={frame->zoom,frame->rotation,frame->warp,frame->warp_speed,
            frame->warp_scale,frame->decay,0,0,0,seconds};
        v[23]=frame->dx; v[24]=frame->dy; v[25]=frame->cx; v[26]=frame->cy;
        v[27]=frame->sx; v[28]=frame->sy; v[29]=frame->zoomexp;
        if(signal) memcpy(v+10,signal->values,MD_SIGNAL_COUNT*sizeof(float));
        md_inputs(v);
        memcpy(v+PM_Q_BASE,q,sizeof(q));memcpy(v+PM_USER_BASE,users,sizeof(users));
        v[PM_META_BASE]=state->frames?(float)(state->frames-1):0;
        v[PM_META_BASE+1]=state->fps;
        v[PM_DYNAMIC_BASE]=(float)state->wave_mode;
        memcpy(v+PM_DYNAMIC_BASE+1,state->motion,sizeof(state->motion));
        memcpy(v+PM_EFFECT_BASE,state->effects,sizeof(state->effects));
        v[PM_ENGINE_BASE+2]=fminf(1,fmaxf(0,seconds/(md_preset_duration>0?md_preset_duration:60)));
        float px=2.0f*x/MD_GRID-1, py=1-2.0f*y/MD_GRID;
        v[PM_COORD_BASE]=(float)x/MD_GRID; v[PM_COORD_BASE+1]=(float)y/MD_GRID;
        v[PM_COORD_BASE+2]=sqrtf(px*px+py*py);
        v[PM_COORD_BASE+3]=(px==0 && py==0)?0:atan2f(py,px);
        int line=0;
        if(!pm_execute_runtime(&p->pixel_program,v,&line,&runtime))
            return md_file_error(error,MD_FILE_INVALID,line,"pixel formula");
        const int ids[]={0,1,2,23,24,25,26,27,28,29};
        const float lo[]={.1f,-.2f,-4,-1,-1,0,0,.25f,.25f,.5f};
        const float hi[]={64,.2f,4,1,1,1,1,4,4,2};
        for(int i=0;i<10;i++) if(!isfinite(v[ids[i]]) || v[ids[i]]<lo[i] || v[ids[i]]>hi[i])
            return md_file_error(error,MD_FILE_INVALID,pm_assignment_line(&p->pixel_program,ids[i]),"pixel range");
        next[y*(MD_GRID+1)+x]=(MdPreset){v[0],v[1],v[2],v[3],v[4],v[5],
            v[23],v[24],v[25],v[26],v[27],v[28],v[29]};
        memcpy(q,v+PM_Q_BASE,sizeof(q));memcpy(users,v+PM_USER_BASE,sizeof(users));
    }
    memcpy(points,next,sizeof(next));
    state->pixel_runtime=runtime;memcpy(state->pixel_user,users,sizeof(users));
    return MD_FILE_OK;
}

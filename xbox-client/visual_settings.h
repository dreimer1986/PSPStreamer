/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "milkdrop_warp.h"
#include "milkdrop_preset.h"
#include "cave_visual.h"
#include "preset_sequence.h"
static int visual_mode; /* Spectrum / Monkey / MilkDrop */
static int visual_auto,visual_seconds=60,visual_fade=1500,visual_random_seconds=10;
static int visual_hard_cuts,visual_hard_threshold=250,visual_hard_seconds=60;
static char visual_preset[256]="active.milk";
typedef struct {const char *key,*en,*de;int *value,lo,hi,step;} XboxVisualOption;
static XboxVisualOption visual_options[]={
    {"cave_fog","Fog","Nebel",&cave_options.fog,0,1,1},
    {"cave_multitexture","Multitexture","Multitextur",&cave_options.multitexture,0,1,1},
    {"cave_hair","Hair","Haare",&cave_options.hair,0,1,1},
    {"cave_transparent_hair","Transparent hair","Transparente Haare",&cave_options.transparent_hair,0,1,1},
    {"cave_beat","Beat response","Beat-Reaktion",&cave_options.beat,0,1,1},
    {"cave_sensitivity","Beat sensitivity","Beat-Empfindlichkeit",&cave_options.sensitivity,0,16,1},
    {"cave_amplitude","Beat amplitude","Beat-Stärke",&cave_options.amplitude,0,16,1},
    {"cave_style","Style (-1 = automatic)","Stil (-1 = automatisch)",&cave_options.style,-1,8,1},
    {"cave_speed","Flight speed %","Flugtempo %",&cave_options.speed,10,200,10},
    {"cave_invert_y","Invert flight Y","Flug-Y invertieren",&cave_options.invert_y,0,1,1},
    {"cave_noise","Noise","Rauschen",&cave_options.noise,0,16,1},
    {"cave_flight_sensitivity","Flight sensitivity","Flug-Empfindlichkeit",&cave_options.flight_sensitivity,10,100,5},
    {"cave_flight_inertia","Flight inertia","Flugträgheit",&cave_options.flight_inertia,0,100,5},
    {"cave_autopilot_ship","Autopilot ship","Autopilot-Schiff",&cave_options.autopilot_ship,0,1,1},
    {"cave_rumble_music","Music rumble %","Musik-Vibration %",&cave_options.rumble_music,0,100,5},
    {"cave_rumble_game","Game rumble %","Spiel-Vibration %",&cave_options.rumble_game,0,100,5},
    {"preset_live_transitions","Live transitions","Live-Übergänge",&md_live_transitions,0,1,1},
    {"preset_auto","Auto: off / ordered / random / rating","Auto: aus / Reihe / Zufall / Wertung",&visual_auto,0,3,1},
    {"preset_seconds","Preset duration (s)","Preset-Dauer (s)",&visual_seconds,30,600,10},
    {"preset_fade_ms","Transition (ms)","Übergang (ms)",&visual_fade,0,5000,250},
    {"preset_random_seconds","Random extra time (s)","Zufällige Zusatzzeit (s)",&visual_random_seconds,0,120,5},
    {"preset_hard_cuts","Hard cuts","Harte Übergänge",&visual_hard_cuts,0,1,1},
    {"preset_hard_threshold","Hard-cut threshold %","Schwellwert %",&visual_hard_threshold,125,400,10},
    {"preset_hard_seconds","Threshold half-life (s)","Schwellwert-Halbwertzeit (s)",&visual_hard_seconds,5,180,5},
    {"visual_resolution","Feedback: 512x256 / 512x512","Effektpuffer: 512x256 / 512x512",&md_high_resolution,0,1,1},
};
#define X_VISUAL_OPTIONS ((int)(sizeof(visual_options)/sizeof(visual_options[0])))
static const char *visual_text(const char *en,const char *de){return ui_language==2||(!ui_language&&dashboard_german)?de:en;}
static const char *visual_name(void){return visual_mode==1?"Monkey":visual_mode==2?"MilkDrop":visual_text("Spectrum","Spektrum");}
static int visual_preferences_write(FILE *f){
    if(fprintf(f,"visualization=%d\nvisual_preset=%s\n",visual_mode,visual_preset)<0)return 0;
    for(int i=0;i<X_VISUAL_OPTIONS;i++)if(fprintf(f,"%s=%d\n",visual_options[i].key,*visual_options[i].value)<0)return 0;return 1;
}
static void visual_preferences_read(const char *key,const char *value){
    if(!strcmp(key,"visual_preset")){char path[256];snprintf(path,sizeof(path),"%s",value);path[strcspn(path,"\r\n")]=0;if(preset_path_valid(path))strcpy(visual_preset,path);return;}
    char *end;long n=strtol(value,&end,10);if(end==value)return;
    if(!strcmp(key,"visualization")){if(n>=0&&n<=2)visual_mode=n;return;}
    for(int i=0;i<X_VISUAL_OPTIONS;i++){XboxVisualOption *o=visual_options+i;if(!strcmp(key,o->key)){if(n>=o->lo&&n<=o->hi)*o->value=n;return;}}
}

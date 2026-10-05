/* SPDX-License-Identifier: GPL-2.0-or-later
 * Xbox input/presentation around unchanged PSP effects and preset evaluator. */
#include "visual_gpu.h"
#include "milkdrop_wave.h"
extern int xbox_cave_phase(void);
enum {PANEL_VISUAL=9,PANEL_EFFECT=10,PANEL_PRESETS=11};
static SDL_Texture *visual_texture;
static unsigned char *visual_font;
static int visual_started,visual_started_mode=-1,visual_loaded,visual_fault;
static PresetSequence *visual_sequence;
static PresetCatalog *visual_catalog;
static char visual_directory[256];
static unsigned long long visual_next;
static MdSignalState visual_cut_signal;
static float visual_cut_threshold;
static unsigned long long visual_cut_tick;
static void visual_trace(const char *message,int persist){if(persist)startup_note(message);}
void xbox_visual_stop(void){
    int was_started=visual_started;
    if(was_started)startup_note("Visualization stop: begin");
    if(visual_started&&diagnostics_enabled){
        FILE *log=fopen("D:\\xbox-player.log","a");if(log){char line[2048];for(int i=0;md_profile_report(i,line,sizeof(line));i++)fputs(line,log);fclose(log);}
    }
    md_stop();md_free_preset(&md_custom_preset);visual_started=visual_loaded=0;visual_started_mode=-1;visual_next=0;
    if(visual_texture)SDL_DestroyTexture(visual_texture);visual_texture=NULL;
    free(visual_font);visual_font=NULL;free(visual_sequence);visual_sequence=NULL;
    free(visual_catalog);visual_catalog=NULL;
    memset(&visual_cut_signal,0,sizeof(visual_cut_signal));visual_cut_tick=0;visual_cut_threshold=0;
    visual_fault=0;
    for(int i=0;i<4;i++)if(pads[i])SDL_GameControllerRumble(pads[i],0,0,0);
    if(was_started)startup_note("Visualization stop: resources released");
}
static void visual_reset(void){xbox_visual_stop();}
static unsigned long long visual_interval(void){return (unsigned long long)visual_seconds*1000000+(unsigned long long)visual_random_seconds*(rand()%1000)*1000;}
static int visual_load(const char *name,int transition){
    if(!preset_path_valid(name))return 0;
    char path[300];snprintf(path,sizeof(path),"D:/presets/%s",name);
    MdFileError error;int result=md_load_transition(path,transition?visual_fade:0,&error);
    if(result!=MD_FILE_OK){snprintf(status,sizeof(status),"MilkDrop L%d: %s (%d)",error.line,error.key,error.code);startup_note(status);return 0;}
    snprintf(visual_preset,sizeof(visual_preset),"%s",name);visual_loaded=1;
    md_profile_select(visual_preset,1,fullscreen,3);
    visual_next=sceKernelGetSystemTimeWide()+visual_interval();md_preset_duration=visual_seconds;return 1;
}
static int visual_begin(void){
    if(visual_started&&visual_started_mode==visual_mode)return 1;
    visual_reset();md_trace_hook=visual_trace;
    startup_note(visual_mode==1?"Monkey: GPU initialize":"MilkDrop: GPU initialize");
    if(!md_start()){snprintf(status,sizeof(status),"%s",xv_error());visual_fault=1;return 0;}
    visual_started=1;visual_started_mode=visual_mode;
    MM_STATISTICS memory={0};memory.Length=sizeof(memory);MmQueryStatistics(&memory);
    char memnote[96];snprintf(memnote,sizeof(memnote),"Visualization memory: available_pages=%lu",(unsigned long)memory.AvailablePages);startup_note(memnote);
    FILE *f=fopen("D:\\visual-font.raw","rb");
    if(f){visual_font=malloc(256*320);if(!visual_font||fread(visual_font,1,256*320,f)!=256*320){free(visual_font);visual_font=NULL;}fclose(f);}
    if(visual_mode==2&&!visual_load(visual_preset,0)){md_stop();visual_started=0;visual_fault=1;return 0;}
    visual_texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,720,480);
    if(!visual_texture){snprintf(status,sizeof(status),"Visual texture: %s",SDL_GetError());md_stop();visual_started=0;visual_fault=1;return 0;}
    SDL_SetTextureBlendMode(visual_texture,SDL_BLENDMODE_NONE);
    md_profile_reset(diagnostics_enabled);md_profile_select(visual_mode==1?"Monkey":visual_preset,1,fullscreen,visual_mode==1?5:3);
    startup_note("Visualization: NV2A offscreen ready");return 1;
}
static void visual_catalog_open(void){
    if(!visual_catalog)visual_catalog=calloc(1,sizeof(*visual_catalog));
    if(!visual_catalog)return;
    char path[300];snprintf(path,sizeof(path),"D:/presets/%s",visual_directory);
    preset_catalog_browse(visual_catalog,path,*visual_directory!=0);panel=PANEL_PRESETS;panel_row=0;
}
static int visual_input(int button){
    if(!playing||!media_audio||visual_mode!=1||!visual_started||panel||controls)return 0;
    int phase=xbox_cave_phase();
    if(phase==CAVE_GAME_OFF)return 0;
    int move=button==SDL_CONTROLLER_BUTTON_DPAD_UP?-1:button==SDL_CONTROLLER_BUTTON_DPAD_DOWN?1:0;
    int ship=button==SDL_CONTROLLER_BUTTON_DPAD_LEFT?-1:button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT?1:0;
    if(button==SDL_CONTROLLER_BUTTON_Y){fullscreen=!fullscreen;return 1;}
    if(button==SDL_CONTROLLER_BUTTON_START){player_pause();return 1;}
    md_cave_game_menu(move,ship,button==SDL_CONTROLLER_BUTTON_A,button==SDL_CONTROLLER_BUTTON_B);
    return 1;
}
static void visual_control_tick(void){
    if(!visual_started||visual_mode!=1)return;
    int shoulders=0,x=128,y=128,throttle=0,roll=0,fire=0;
    if(!panel&&!controls)for(int i=0;i<4;i++)if(pads[i]){
        SDL_GameController *pad=pads[i];
        int l=SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_TRIGGERLEFT)>16000;
        int r=SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_TRIGGERRIGHT)>16000;
        shoulders=l&&r;roll=shoulders?0:r?1:l?-1:0;
        x=128+SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTX)/256;
        y=128+SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTY)/256;
        throttle=SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_DPAD_UP)?1:SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_DPAD_DOWN)?-1:0;
        fire=SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_A);break;
    }
    md_cave_control(shoulders,x,y,throttle,roll,fire,sceKernelGetSystemTimeWide());
    const char *labels[]={visual_text("SHIELD","SCHILD"),visual_text("Score","Punkte"),"Hall of Fame","A / B: OK",visual_text("Save failed","Speichern fehlgeschlagen"),"GAME OVER",visual_text("Game Start","Spiel starten"),visual_text("Exit","Ende"),"LT + RT: 5s",visual_text("No scores yet","Noch keine Punkte"),"UP / DOWN   A: OK   B: Exit"};
    md_cave_game_ui(visual_font,paused||panel||controls,labels);
    unsigned small,large;md_cave_rumble(playing&&!paused&&!panel&&!controls,&small,&large);
    for(int i=0;i<4;i++)if(pads[i])SDL_GameControllerRumble(pads[i],large*65535/255,small?65535:0,100);
}
static void visual_auto_tick(const unsigned char *bands,unsigned long long now){
    if(!visual_auto||paused)return;
    if(!visual_sequence){visual_sequence=calloc(1,sizeof(*visual_sequence));if(visual_sequence)preset_sequence_load_selected(visual_sequence,"D:/presets",visual_preset,(unsigned)now);}
    if(!visual_sequence)return;
    int hard=0;
    if(visual_hard_cuts){float base=visual_hard_threshold*.01f,dt=visual_cut_tick?(now-visual_cut_tick)*.000001f:0;
        if(!visual_cut_tick)visual_cut_threshold=base*2;visual_cut_tick=now;
        md_signal_update(&visual_cut_signal,bands,0,now);
        if(visual_cut_signal.signal.values[7]+visual_cut_signal.signal.values[8]+visual_cut_signal.signal.values[9]>visual_cut_threshold*3){hard=1;visual_cut_threshold*=2;}
        visual_cut_threshold=base+(visual_cut_threshold-base)*expf(-1.3863f*dt/visual_hard_seconds);
    }
    if(now<visual_next&&!hard)return;
    int index=preset_sequence_next(visual_sequence,visual_preset,visual_auto);
    if(index>=0){int fade=visual_fade;if(hard)visual_fade=0;if(!visual_load(visual_sequence->catalog.names[index],1))visual_sequence->rating[index]=-1;visual_fade=fade;}
    visual_next=now+visual_interval();
}
static void visual_draw(int full){
    if(!visual_mode){if(visual_started)visual_reset();spectrum_draw(full);return;}
    if(visual_fault){text_at(status,35,160,492,(SDL_Color){255,160,100,255});return;}
    if(!visual_begin())return;
    unsigned idx=ac97[0x114]&31;unsigned long long now=sceKernelGetSystemTimeWide();
    int active=audio_running&&!paused&&!underrun;
    spectrum_analysis_output(0);
    if(active){spectrum_pcm_publish(audio_waveform[idx],1152);visualization_pcm_publish(audio_waveform[idx],1152);}
    spectrum_analysis_step(now,1,active);
    unsigned char bands[12];int peak=0;for(int i=0;i<12;i++){bands[i]=active?legacy_level(audio_waveform[idx],i):0;if(bands[i]>peak)peak=bands[i];}
    visual_control_tick();
    if(visual_mode==2)visual_auto_tick(bands,now);
    xv_width=full?720:506;xv_height=full?480:232;
    if(!paused){
        md_title(visual_font,media_artist,media_name,now,0);
        int result=md_frame(1,full,bands,peak,now,visual_mode==1?5:3);
        if(result<=0||xv_failed()){
            if(xv_failed())snprintf(status,sizeof(status),"%s",xv_error());
            else snprintf(status,sizeof(status),"MilkDrop L%d: %s (%d)",md_runtime_error.line,md_runtime_error.key,md_runtime_error.code);
            startup_note(status);xbox_visual_stop();visual_fault=1;return;
        }
    }
    SDL_Rect src={0,0,xv_width,xv_height};
    SDL_Rect dst=full?(SDL_Rect){0,0,720,480}:(SDL_Rect){27,60,506,232};
    if(visual_present_pixels((const uint32_t*)xv_pixels(),768,xv_width,xv_height,dst,0))return;
    SDL_UpdateTexture(visual_texture,&src,xv_pixels(),768*4);
    SDL_RenderCopy(renderer,visual_texture,&src,&dst);
}
static void visual_panel_draw(SDL_Color normal,SDL_Color selected){
    if(panel==PANEL_VISUAL){const char *rows[]={visual_name(),visual_text("Effect settings","Effekt-Einstellungen"),visual_text("MilkDrop presets","MilkDrop-Presets"),XL(SPECTRUM_SETTINGS)};
        for(int i=0;i<4;i++)text_at(rows[i],35,68+i*32,492,i==panel_row?selected:normal);
        text_at(visual_text("Monkey: LT + RT opens the flight game","Monkey: LT + RT öffnet das Flugspiel"),35,210,492,normal);
        text_at(visual_text("Hold LT + RT for 5 seconds to leave it","Zum Verlassen LT + RT 5 Sekunden halten"),35,240,492,normal);
    }else if(panel==PANEL_EFFECT){int first=panel_row/8*8;
        for(int i=first;i<X_VISUAL_OPTIONS&&i<first+8;i++){XboxVisualOption *o=visual_options+i;char text[128];snprintf(text,sizeof(text),"%s: %d",visual_text(o->en,o->de),*o->value);text_at(text,35,64+(i-first)*28,492,i==panel_row?selected:normal);}
    }else if(panel==PANEL_PRESETS&&visual_catalog){
        int first=panel_row/8*8;for(int i=first;i<visual_catalog->count&&i<first+8;i++)text_at(visual_catalog->names[i],35,64+(i-first)*28,492,i==panel_row?selected:normal);
        if(!visual_catalog->count)text_at(visual_text("No .milk presets in D:/presets","Keine .milk-Presets in D:/presets"),35,80,492,normal);
    }
}
static int visual_panel_input(int button){
    if(panel<PANEL_VISUAL||panel>PANEL_PRESETS)return 0;
    if(button==SDL_CONTROLLER_BUTTON_B){panel=panel==PANEL_VISUAL?0:PANEL_VISUAL;panel_row=0;preferences(1);return 1;}
    int count=panel==PANEL_VISUAL?4:panel==PANEL_EFFECT?X_VISUAL_OPTIONS:visual_catalog?visual_catalog->count:0;
    if(!count)return 1;
    if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)panel_row=(panel_row+count-1)%count;
    if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)panel_row=(panel_row+1)%count;
    if(button!=SDL_CONTROLLER_BUTTON_A&&button!=SDL_CONTROLLER_BUTTON_DPAD_LEFT&&button!=SDL_CONTROLLER_BUTTON_DPAD_RIGHT)return 1;
    int direction=button==SDL_CONTROLLER_BUTTON_DPAD_LEFT?-1:1;
    if(panel==PANEL_VISUAL){if(panel_row==0){visual_mode=(visual_mode+3+direction)%3;visual_reset();}
        else if(panel_row==1){panel=PANEL_EFFECT;panel_row=visual_mode==2?16:0;}
        else if(panel_row==2)visual_catalog_open();else{panel=4;panel_row=0;}}
    else if(panel==PANEL_EFFECT){XboxVisualOption *o=visual_options+panel_row;int n=*o->value+direction*o->step;*o->value=n<o->lo?o->hi:n>o->hi?o->lo:n;free(visual_sequence);visual_sequence=NULL;}
    else if(button==SDL_CONTROLLER_BUTTON_A){
        char name[256];strcpy(name,visual_catalog->names[panel_row]);size_t n=strlen(name);
        if(!strcmp(name,"..")){char *p=strrchr(visual_directory,'/');if(p)*p=0;else *visual_directory=0;visual_catalog_open();}
        else if(n&&name[n-1]=='/'){name[n-1]=0;char path[256];int size=snprintf(path,sizeof(path),"%s%s%s",visual_directory,*visual_directory?"/":"",name);if(size<(int)sizeof(path)){strcpy(visual_directory,path);visual_catalog_open();}}
        else{char path[256];int size=snprintf(path,sizeof(path),"%s%s%s",visual_directory,*visual_directory?"/":"",name);
            if(size<(int)sizeof(path)&&preset_path_valid(path)){
                if(visual_mode!=2||!visual_started){visual_mode=2;visual_reset();strcpy(visual_preset,path);}
                else visual_load(path,1);
                free(visual_sequence);visual_sequence=NULL;panel=0;preferences(1);
            }}
    }return 1;
}

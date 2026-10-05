/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <hal/debug.h>
#include <hal/video.h>
#include <hal/xbox.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <nxdk/net.h>
#include <lwip/sockets.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <math.h>
#include "language.h"
#include "net.h"
#include "catalog.h"
#include "player.h"
#include "visual_settings.h"
#include "settings.h"
#include "report.h"
#include "remote.h"
#include "server_settings.h"
#include "render_clip.h"
#include "text_cache.h"

static void menu_draw(void);

static SDL_Window *window;static SDL_Renderer *renderer;static TTF_Font *font;static SDL_Texture *skin;
static SDL_GameController *pads[4];
static int width=640,height=480,options,fullscreen=1,quit;
static char status[160];static float meters[2];
static int first_frame=1;
static int panel,panel_row,autoplay_pending,media_live,retry_count,retry_pending;
static int controls,control_row,remote_play_pending,remote_audio,remote_subtitle;
static double remote_start;
static int next_pending;static Uint32 next_at;
static Uint32 search_refresh_at;
static int queued_audio=-1,queued_subtitle=-2;
static double start_position,retry_position;static Uint32 retry_at;
static SDL_Texture *cover,*backdrop;
static char carry_audio[16],carry_subtitle[16];static int carry_tracks,carry_subtitle_off;
static void startup_note(const char *stage){
    if(!diagnostics_enabled)return;
    if(KdDebuggerEnabled)DbgPrint("PSPX thread=%p tick=%lu: %s\n",KeGetCurrentThread(),(unsigned long)GetTickCount(),stage);
    FILE *f=fopen("D:\\xbox-player.log","a");if(f){fprintf(f,"startup: %s | SDL: %s\n",stage,SDL_GetError());fclose(f);}
}
static SDL_AssertState assertion_log(const SDL_AssertData *data,void *unused){
    (void)unused;FILE *f=fopen("D:\\xbox-player.log","a");if(f){fprintf(f,"SDL assertion: %s at %s:%d (%s)\n",data->condition,data->filename,data->linenum,data->function);fclose(f);}return SDL_ASSERTION_ABORT;
}
/* The SDK's C assert handler stops the CPU with cli/hlt, without saving a
 * diagnostic. SDL's assertion handler does not cover these USB/libc asserts.
 * Preserve the failure on disk before retaining the SDK's fatal-stop policy. */
void _xbox_assert(const char *expression,const char *file,const char *function,unsigned long line){
    if(KdDebuggerEnabled)DbgPrint("SDK assertion: %s at %s:%lu (%s)\n",expression,file,line,function);
    if(KeGetCurrentIrql()==0){
        FILE *f=fopen("D:\\xbox-player.log","a");
        if(f){fprintf(f,"SDK assertion: %s at %s:%lu (%s)\n",expression,file,line,function);fclose(f);}
    }
    debugClearScreen();debugResetCursor();
    debugPrint("SDK assertion: %s\n%s:%lu\n%s\nSee xbox-player.log\n",expression,file,line,function);
    __asm__ __volatile__("cli\n1: hlt\njmp 1b");
    __builtin_unreachable();
}
static void controller_open(int device){
    SDL_JoystickID id=SDL_JoystickGetDeviceInstanceID(device);
    if(id<0)return;
    for(int i=0;i<4;i++)if(pads[i]&&SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i]))==id)return;
    startup_note("controller: opening device");
    for(int i=0;i<4;i++)if(!pads[i]){pads[i]=SDL_GameControllerOpen(device);break;}
    startup_note("controller: open returned");
}
/* nxdk's pinned SDL software backend requires SETVIEWPORT before SETCLIPRECT
 * in EACH command batch. Destroying a temporary text texture flushes a batch;
 * plain RenderSetClipRect then queues the clip BEFORE the next draw's viewport
 * and triggers an invisible assertion dialog before the first Present. */
static void clip(const SDL_Rect *rect){xbox_render_clip(renderer,rect);}
typedef struct {SDL_Thread *thread;SDL_atomic_t cancel,done;char path[6000],error[100],post[4096];char *body;unsigned size;int type;} Fetch;
static Fetch fetch;
static int fetch_worker(void *unused){
    (void)unused;Http h;
    startup_note("catalog: worker entered, opening HTTP");
    if(!http_request(&h,fetch.path,&fetch.cancel,*fetch.post?fetch.post:NULL,fetch.type==3?15000:180000)){snprintf(fetch.error,sizeof(fetch.error),"HTTP %d / connection failed",h.status);goto done;}
    startup_note("catalog: HTTP headers received");
    unsigned cap=fetch.type==3?2*1024*1024:512*1024;
    fetch.body=malloc(cap);if(!fetch.body){snprintf(fetch.error,sizeof(fetch.error),"Out of response memory");http_close(&h);goto done;}
    unsigned at=0;int n=0;
    while(at<cap-1&&(n=http_read(&h,fetch.body+at,cap-1-at))>0)at+=n;
    fetch.body[at]=0;fetch.size=at;http_close(&h);
    if(n<0||at==cap-1)snprintf(fetch.error,sizeof(fetch.error),"Incomplete/oversized library reply");
done:startup_note(*fetch.error?fetch.error:"catalog: response complete");SDL_AtomicSet(&fetch.done,1);return 0;
}
static void fetch_stop(void){if(fetch.thread){SDL_AtomicSet(&fetch.cancel,1);SDL_WaitThread(fetch.thread,NULL);fetch.thread=NULL;}free(fetch.body);fetch.body=NULL;config_restore_test();}
static void fetch_request(const char *path,int type,const char *post){
    startup_note("catalog: request queued");
    fetch_stop();memset(&fetch,0,sizeof(fetch));fetch.type=type;snprintf(fetch.path,sizeof(fetch.path),"%s",path);
    if(post)snprintf(fetch.post,sizeof(fetch.post),"%s",post);
    snprintf(status,sizeof(status),XL(LOADING_CANCEL));
    startup_note("catalog: drawing loading status");menu_draw();
    startup_note("catalog: creating worker");
    fetch.thread=SDL_CreateThreadWithStackSize(fetch_worker,"catalog",65536,NULL);
    startup_note(fetch.thread?"catalog: worker created":"catalog: worker creation failed");
    if(!fetch.thread)snprintf(status,sizeof(status),XL(WORKER_FAILED));
}
static void fetch_start(const char *path,int type){fetch_request(path,type,NULL);}
static void browse(const char *path,int offset){char encoded[4700],request[6000],query[400];if(!url_encode(path,encoded,sizeof(encoded))){snprintf(status,sizeof(status),XL(PATH_LONG));return;}url_encode(search_query,query,sizeof(query));snprintf(request,sizeof(request),"/api/xbox/library?root=%d&path=%s&offset=%d&q=%s",root_index,encoded,offset,query);fetch_start(request,0);}
static int in_queue(void){return !strcmp(folder_path,":xbox:queue:");}
static void library_action(int row){
    Entry *e=entry_count?&entries[entry_index]:NULL;char body[4096],hex[3073],name_hex[513];
    if(row<5&&(!e||e->folder))return;
    static const char digits[]="0123456789abcdef";
    unsigned i=0;if(e)for(;e->target[i]&&i<1536;i++){unsigned c=(unsigned char)e->target[i];hex[i*2]=digits[c>>4];hex[i*2+1]=digits[c&15];}hex[i*2]=0;
    i=0;if(e)for(;e->name[i]&&i<256;i++){unsigned c=(unsigned char)e->name[i];name_hex[i*2]=digits[c>>4];name_hex[i*2+1]=digits[c&15];}name_hex[i*2]=0;
    if(row==0)snprintf(body,sizeof(body),"{\"action\":\"favorite\",\"id_hex\":\"%s\",\"name_hex\":\"%s\",\"value\":%s}",hex,name_hex,e->favorite?"false":"true");
    else if(row==1||row==2)snprintf(body,sizeof(body),"{\"action\":\"%s\",\"revision\":%d,\"id_hex\":\"%s\",\"name_hex\":\"%s\"}",row==1?"add":"remove",queue_revision,hex,name_hex);
    else if(row==3||row==4){int pos=e->position+(row==3?-1:1);if(pos<0||pos>=total_entries)return;snprintf(body,sizeof(body),"{\"action\":\"move\",\"revision\":%d,\"id_hex\":\"%s\",\"position\":%d}",queue_revision,hex,pos);}
    else if(row==5)snprintf(body,sizeof(body),"{\"action\":\"enabled\",\"revision\":%d,\"enabled\":%s}",queue_revision,queue_enabled?"false":"true");
    else if(row==6)snprintf(body,sizeof(body),"{\"action\":\"repeat\",\"revision\":%d,\"repeat\":%d}",queue_revision,(queue_repeat+1)%3);
    else snprintf(body,sizeof(body),"{\"action\":\"shuffle\",\"revision\":%d,\"shuffle\":%s}",queue_revision,queue_shuffle?"false":"true");
    fetch_request("/api/xbox/library-action",5,body);
}
static void server_editor(void){
    fetch_stop();remote_shutdown();SDL_AtomicSet(&remote_cancel,0);
    report_shutdown();SDL_AtomicSet(&report_cancel,0);report_count=0;
    strcpy(server_draft_host,host);strcpy(server_draft_password,password);snprintf(server_draft_port,sizeof(server_draft_port),"%u",port);
    panel=5;panel_row=0;
}
static void select_media(Entry *e){
    if(!autoplay_pending)carry_tracks=0;
    if(cover)SDL_DestroyTexture(cover);cover=NULL;if(backdrop)SDL_DestroyTexture(backdrop);backdrop=NULL;
    snprintf(media_id,sizeof(media_id),"%s",e->target);snprintf(media_name,sizeof(media_name),"%s",e->name);media_audio=e->audio;
    media_live=!strncmp(media_id,"radio.",6);retry_count=retry_pending=0;
    char encoded[4700],request[6000];url_encode(media_id,encoded,sizeof(encoded));snprintf(request,sizeof(request),"/api/xbox/metadata/%s?queue=%d",encoded,in_queue());fetch_start(request,1);
}
static void next_media(int previous){
    if(media_live)return;
    int manual=playing;
    carry_tracks=1;snprintf(carry_audio,sizeof(carry_audio),"%s",audio_count?audio_languages[audio_track]:"");
    carry_subtitle_off=subtitle_track<0;snprintf(carry_subtitle,sizeof(carry_subtitle),"%s",subtitle_track>=0?subtitle_languages[subtitle_track]:"");
    report_playback("stopped");report_started=0;player_stop();retry_pending=0;
    char encoded[4700],request[6000];url_encode(media_id,encoded,sizeof(encoded));
    snprintf(request,sizeof(request),"/api/xbox/next/%s?direction=%s&shuffle=%d&manual=%d&repeat_one=%d",encoded,previous?"previous":"next",media_audio&&shuffle_music&&!previous,previous||manual,repeat_one);
    fetch_start(request,2);
}
static void begin_playback(double position){
    controls=next_pending=0;
    start_position=position;report_started=0;retry_count=retry_pending=0;panel=0;fullscreen=!media_audio;
    audio_analysis=media_audio&&show_spectrum;
    preferences(1);
    if(!player_start(position))snprintf(status,sizeof(status),XL(PLAYER_FAILED));else snprintf(status,sizeof(status),XL(BUFFERING));
}
static void apply_metadata_preferences(void){
    for(int i=0;i<audio_count;i++)if(prefer_audio&&!strcmp(audio_languages[i],language_codes[prefer_audio])){audio_track=i;break;}
    for(int i=0;i<subtitle_count;i++)if(prefer_subtitle&&!strcmp(subtitle_languages[i],language_codes[prefer_subtitle])){subtitle_track=i;break;}
    if(carry_tracks){
        for(int i=0;i<audio_count;i++)if(!strcmp(audio_languages[i],carry_audio)){audio_track=i;break;}
        if(carry_subtitle_off)subtitle_track=-1;
        else for(int i=0;i<subtitle_count;i++)if(!strcmp(subtitle_languages[i],carry_subtitle)){subtitle_track=i;break;}
        carry_tracks=0;
    }
    if(suggested_audio>=0&&suggested_audio<audio_count)audio_track=suggested_audio;
    if(suggested_subtitle>=-1&&suggested_subtitle<subtitle_count)subtitle_track=suggested_subtitle;
    if(queued_audio>=0&&queued_audio<audio_count)audio_track=queued_audio;
    if(queued_subtitle>=-1&&queued_subtitle<subtitle_count)subtitle_track=queued_subtitle;
    queued_audio=-1;queued_subtitle=-2;
    start_position=media_resume;
}
static void artwork_read(void){
    if(fetch.size<20||memcmp(fetch.body,"XART",4))return;
    uint16_t dims[4];uint32_t sizes[2];memcpy(dims,fetch.body+4,8);memcpy(sizes,fetch.body+12,8);
    if(dims[0]!=1280||dims[1]!=720||dims[2]!=240||dims[3]!=336||
        (sizes[0]&&sizes[0]!=1280*720*2)||(sizes[1]&&sizes[1]!=240*336*2)||20+sizes[0]+sizes[1]!=fetch.size)return;
    unsigned at=20;for(int i=0;i<2;i++){if(sizes[i]){
        SDL_Surface *s=SDL_CreateRGBSurfaceFrom(fetch.body+at,dims[i*2],dims[i*2+1],16,dims[i*2]*2,0xf800,0x07e0,0x001f,0);
        if(s){SDL_Texture *t=SDL_CreateTextureFromSurface(renderer,s);SDL_FreeSurface(s);if(i)cover=t;else if(t){backdrop=t;SDL_SetTextureBlendMode(t,SDL_BLENDMODE_BLEND);SDL_SetTextureAlphaMod(t,70);}}
    }at+=sizes[i];}
}
static void color(int r,int g,int b){SDL_SetRenderDrawColor(renderer,r,g,b,255);}
static void text_at(const char *s,int x,int y,int max_width,SDL_Color color){
    text_cached_draw(renderer,font,s,x,y,max_width,color);
}
static void side(const char *s,int row){SDL_Color c={220,226,227,255};text_at(s,565,68+row*24,120,c);}
static void wrapped(const char *s,int y,int lines){
    /* UTF-8 wrap by measured glyph width, never slice inside a codepoint. */
    SDL_Color c={225,234,237,255};
    while(*s&&lines--){char line[512];size_t n=0,last=0;int w=0,h;
        while(s[n]&&n<sizeof(line)-5){size_t bytes=1;unsigned ch=(unsigned char)s[n];if(ch>=0xc0)bytes=ch<0xe0?2:ch<0xf0?3:4;
            if(strlen(s+n)<bytes)break;memcpy(line+n,s+n,bytes);n+=bytes;line[n]=0;TTF_SizeUTF8(font,line,&w,&h);
            if(w>480){n-=bytes;break;}if(s[n-1]==' ')last=n;
        }
        if(!n)break;if(s[n]&&last)n=last;memcpy(line,s,n);line[n]=0;text_at(line,35,y,492,c);s+=n;while(*s==' ')s++;y+=25;
    }
}
#include "visual_present.h"
#include "spectrum.h"
#include "visual_ui.h"
#include "display.h"
static void seek_playback(double seconds){
    if(!playing||media_live)return;if(seconds<0)seconds=0;if(media_duration>1&&seconds>=media_duration)seconds=media_duration-1;
    player_start(seconds);report_time=0;
}
static void chapter_jump(int forward){
    double pos=player_position(),target=-1;
    if(forward){for(int i=0;i<chapter_count;i++)if(chapter_starts[i]>pos+1){target=chapter_starts[i];break;}}
    else for(int i=chapter_count-1;i>=0;i--)if(chapter_starts[i]<pos-2){target=chapter_starts[i];break;}
    if(target>=0)seek_playback(target);else snprintf(status,sizeof(status),"%s",forward?XL(NO_NEXT_CHAPTER):XL(NO_PREV_CHAPTER));
}
static void controls_draw(void){
    if(!controls)return;
    SDL_RenderSetScale(renderer,(float)width/720,(float)height/480);
    color(12,19,27);SDL_Rect r={75,65,570,350};SDL_RenderFillRect(renderer,&r);
    const char *labels[]={paused?XL(RESUME):XL(PAUSE),XL(BACK_30),XL(FORWARD_30),chapter_count?XL(PREV_CHAPTER):XL(NO_PREV_CHAPTER),chapter_count?XL(NEXT_CHAPTER):XL(NO_NEXT_CHAPTER),XL(PREV_FILE),XL(NEXT_FILE),XL(STOP)};
    for(int i=0;i<8;i++){char line[100];snprintf(line,sizeof(line),"%s%s",i==control_row?"> ":"  ",labels[i]);
        text_at(line,95,78+i*34,530,(SDL_Color){i==control_row?255:210,i==control_row?205:220,180,255});}
    text_at(XL(CONTROLS_HELP),95,375,530,(SDL_Color){190,210,230,255});
}
static void video_progress(void){
    if(!fullscreen||media_live||media_duration<=0)return;
    double pos=player_position();if(pos<0)pos=0;if(pos>media_duration)pos=media_duration;
    SDL_RenderSetScale(renderer,1,1);int margin=width/30,bar=width-2*margin;
    SDL_Rect track={margin,height-10,bar,3};color(45,52,60);SDL_RenderFillRect(renderer,&track);
    track.w=(int)(bar*pos/media_duration);color(255,207,80);SDL_RenderFillRect(renderer,&track);
}
static void receiver(void){
    static const signed char nx[]={-42,-40,-37,-34,-30,-25,-20,-15,-10,-5,0,5,10,15,20,25,30,34,37,40,42};
    static const signed char ny[]={-11,-16,-21,-25,-29,-32,-35,-37,-39,-40,-40,-40,-39,-37,-35,-32,-29,-25,-21,-16,-11};
    unsigned idx=ac97[0x114]&31;
    for(int i=0;i<2;i++){float target=playing&&!paused&&audio_running&&!underrun?audio_peaks[idx][i]/327.68f:0;
        meters[i]+=(target-meters[i])*(target>meters[i]?.45f:.15f);int value=(int)(meters[i]*.2f+.5f);if(value<0)value=0;if(value>20)value=20;
        int x=i?237:90;color(255,201,60);SDL_RenderDrawLine(renderer,x,430,x+nx[value],430+ny[value]);
    }
    static const signed char kx[]={-21,-24,-27,-29,-30,-30,-30,-29,-27,-24,-21,-18,-14,-9,-5,0,5,9,14,18,21,24,27,29,30,30,30,29,27,24,21};
    static const signed char ky[]={21,18,14,9,5,0,-5,-9,-14,-18,-21,-24,-27,-29,-30,-30,-30,-29,-27,-24,-21,-18,-14,-9,-5,0,5,9,14,18,21};
    int v=volume*30/100,x=633+kx[v]*27/32,y=408+ky[v];
    for(int dy=-2;dy<=2;dy++)SDL_RenderDrawLine(renderer,x-(abs(dy)==2?1:2),y+dy,x+(abs(dy)==2?1:2),y+dy);
    for(int i=0;i<5;i++){color(70+(i*33)%130,170,220);SDL_Rect r={331+i*47,387,25,2};SDL_RenderFillRect(renderer,&r);}
}
static void menu_render(void){
    SDL_RenderSetScale(renderer,(float)width/720,(float)height/480);
    color(0,0,0);SDL_RenderClear(renderer);SDL_Rect all={0,0,720,480};SDL_RenderCopy(renderer,skin,NULL,&all);
    SDL_Color normal={225,234,237,255},selected={255,207,80,255};
    if(first_frame)startup_note("theme queued");
    SDL_Rect left={27,60,506,232};clip(&left);
    if(backdrop&&(options||playing||panel==3)){SDL_Rect r={27,60,506,232};SDL_RenderCopy(renderer,backdrop,NULL,&r);}
    if(panel>=PANEL_VISUAL&&panel<=PANEL_PRESETS){visual_panel_draw(normal,selected);
    }else if(panel==1){
        const char *help[]={XL(HELP_PLAY), XL(HELP_CONTROLS), XL(HELP_VOLUME), XL(HELP_SKIP), XL(HELP_SPECTRUM), XL(HELP_MENU), XL(HELP_RESUME), XL(HELP_ACTIONS)};
        for(int i=0;i<8;i++)text_at(help[i],35,64+i*28,492,normal);
    }else if(panel==8){
        Entry *e=entry_count?&entries[entry_index]:NULL;
        char lines[8][140];snprintf(lines[0],140,"%s",e&&e->favorite?XL(FAVORITE_REMOVE):XL(FAVORITE_ADD));
        snprintf(lines[1],140,"%s",XL(QUEUE_ADD));snprintf(lines[2],140,"%s",XL(QUEUE_REMOVE));
        snprintf(lines[3],140,"%s",XL(QUEUE_UP));snprintf(lines[4],140,"%s",XL(QUEUE_DOWN));
        snprintf(lines[5],140,XL(QUEUE_ENABLED),queue_enabled?XL(ON):XL(OFF));
        snprintf(lines[6],140,XL(QUEUE_REPEAT),queue_repeat==2?XL(REPEAT_ALL):queue_repeat?XL(REPEAT_ONE):XL(OFF));
        snprintf(lines[7],140,XL(QUEUE_SHUFFLE),queue_shuffle?XL(ON):XL(OFF));
        for(int i=0;i<8;i++)text_at(lines[i],35,64+i*28,492,i==panel_row?selected:normal);
    }else if(panel==7){
        if(display_confirm){
            text_at(XL(KEEP_OUTPUT),35,95,492,selected);
            text_at(XL(KEEP_OUTPUT_HELP),35,135,492,normal);
            char countdown[80];snprintf(countdown,sizeof(countdown),XL(RESTORE_COUNTDOWN),(unsigned)((display_deadline-SDL_GetTicks()+999)/1000));text_at(countdown,35,175,492,normal);
        }else{
            int first=panel_row/7*7;
            for(int i=first;i<display_count&&i<first+7;i++){char label[100];display_label(&display_modes[i],label,sizeof(label));text_at(label,35,65+(i-first)*28,492,i==panel_row?selected:normal);}
            text_at(XL(OUTPUT_HELP),35,264,492,normal);
        }
    }else if(panel==5){
        char lines[5][150];snprintf(lines[0],150,XL(SERVER_IP),server_draft_host);
        snprintf(lines[1],150,XL(PORT),server_draft_port);snprintf(lines[2],150,XL(PASSWORD),*server_draft_password?"********":XL(EMPTY_PASSWORD));
        strcpy(lines[3],XL(TEST_SERVER));strcpy(lines[4],XL(SAVE_SERVER));
        for(int i=0;i<5;i++)text_at(lines[i],35,70+i*33,492,i==panel_row?selected:normal);
        text_at(XL(EDIT_HELP),35,255,492,normal);
    }else if(panel==6){
        char display[130];snprintf(display,sizeof(display),"%s",keyboard_draft);
        if(keyboard_field==2)memset(display,'*',strlen(display));
        text_at(display,35,63,492,normal);
        static const int extra[]={0xE4,0xF6,0xFC,0xC4,0xD6,0xDC,0xDF};
        for(int i=0;i<102;i++){int cp=i<95?i+32:extra[i-95];char glyph[3]={0};
            if(cp<128)glyph[0]=cp==32?'_':cp;else{glyph[0]=0xC0|(cp>>6);glyph[1]=0x80|(cp&63);}
            text_at(glyph,40+i%12*39,92+i/12*20,35,i==keyboard_key?selected:normal);
        }
    }else if(panel==4){
        const char *styles[]={"Original",XL(RAINBOW),"VU",XL(ICE),XL(FIRE)};char lines[7][100];
        snprintf(lines[0],100,XL(ANALYSIS),spectrum_analysis_mode?XL(FFT):XL(LEGACY));
        snprintf(lines[1],100,XL(BANDS),spectrum_band_count);
        snprintf(lines[2],100,XL(GAIN),spectrum_gain_db);
        snprintf(lines[3],100,XL(COLORS),styles[spectrum_style]);
        snprintf(lines[4],100,XL(LEDS),spectrum_segments?XL(ON):XL(OFF));
        snprintf(lines[5],100,XL(LED_COUNT),spectrum_led_count);
        snprintf(lines[6],100,XL(PEAK),spectrum_peak_hold?XL(ON):XL(OFF));
        for(int i=0;i<7;i++)text_at(lines[i],35,68+i*29,492,i==panel_row?selected:normal);
    }else if(panel==2){
        char lines[19][140];snprintf(lines[0],140,XL(AUTOPLAY),auto_next?XL(ON):XL(OFF));snprintf(lines[1],140,XL(REPEAT_CURRENT),repeat_one?XL(ON):XL(OFF));
        snprintf(lines[2],140,XL(FOLDER_SHUFFLE),shuffle_music?XL(ON):XL(OFF));snprintf(lines[3],140,XL(SPECTRUM),show_spectrum?XL(ON):XL(OFF));
        snprintf(lines[4],140,XL(VIDEO_QUALITY),quality_names[quality]);snprintf(lines[5],140,XL(VOLUME),volume);
        snprintf(lines[6],140,XL(AUDIO),prefer_audio?language_label(language_codes[prefer_audio]):XL(SOURCE_DEFAULT));snprintf(lines[7],140,XL(SUBTITLES),prefer_subtitle?language_label(language_codes[prefer_subtitle]):XL(OFF));
        strcpy(lines[8],XL(SERVER_CONNECTION));strcpy(lines[9],XL(VISUAL_SETTINGS));
        snprintf(lines[10],140,XL(DEBUG),diagnostics_enabled?XL(ON):XL(OFF));snprintf(lines[11],140,XL(NEXT_DELAY),next_delay);
        snprintf(lines[12],140,XL(VIDEO_CODEC),video_codec?"MPEG-2":"MPEG-1");
        snprintf(lines[13],140,XL(TV_SHAPE),display_wide?"16:9 widescreen":"4:3 letterbox");
        snprintf(lines[14],140,XL(OUTPUT),width,height);
        snprintf(lines[15],140,XL(DOWNMIX),matrix_names[audio_matrix]);
        snprintf(lines[16],140,XL(RENDERER),video_hardware?"NV2A overlay (auto)":"Software");
        snprintf(lines[17],140,XL(LANGUAGE),ui_language_name());
        snprintf(lines[18],140,XL(AUDIO_QUALITY),audio_quality_keys[audio_quality]);
        int first=panel_row/8*8;for(int i=first;i<19&&i<first+8;i++)text_at(lines[i],35,64+(i-first)*28,492,i==panel_row?selected:normal);
    }else if(panel==3){
        text_at(media_name,35,66,492,selected);char line[160];snprintf(line,sizeof(line),XL(TRACK_INFO),(int)media_duration/60,(int)media_duration%60,audio_count,subtitle_count);text_at(line,35,96,492,normal);
        if(*media_artist)text_at(media_artist,35,123,492,normal);if(*media_album)text_at(media_album,35,150,492,normal);
        wrapped(*media_summary?media_summary:XL(NO_SYNOPSIS),177,4);
    }else if(playing){
        text_at(media_name,35,70,492,selected);char line[120];double p=player_position();
        snprintf(line,sizeof(line),"%s  %02d:%02d / %02d:%02d",paused?XL(PAUSED):XL(PLAYING),(int)p/60,(int)p%60,(int)media_duration/60,(int)media_duration%60);text_at(line,35,105,492,normal);
        if(media_audio&&show_spectrum&&!(fullscreen&&!panel))visual_draw(0);
        else{text_at(XL(PLAYBACK_HELP),35,160,492,normal);text_at(XL(SEEK_HELP),35,190,492,normal);}
    }else if(options){
        text_at(media_name,35,66,492,selected);
        char lines[4][180];snprintf(lines[0],180,start_position>0?XL(RESUME_AT):XL(PLAY),(int)start_position/60,(int)start_position%60);snprintf(lines[1],180,XL(AUDIO),audio_count?audio_labels[audio_track]:XL(NO_AUDIO));
        snprintf(lines[2],180,XL(SUBTITLES),subtitle_track<0?XL(OFF):subtitle_labels[subtitle_track]);
        snprintf(lines[3],180,XL(VIDEO),quality_names[quality],video_codec?2:1);
        for(int i=0;i<(media_audio?1:4);i++)text_at(lines[i],35,105+i*31,492,i==option_row?selected:normal);
    }else{
        int start=(entry_index/8)*8;
        for(int i=start;i<entry_count&&i<start+8;i++){char line[290];Entry *e=&entries[i];const char *name=e->name;
            if(e->folder){if(!strcmp(e->target,":xbox:favorites:"))name=XL(FAVORITES);else if(!strcmp(e->target,":xbox:recent:"))name=XL(RECENT);else if(!strcmp(e->target,":xbox:queue:"))name=XL(PLAYLIST);else if(!strcmp(e->target,":xbox:search:"))name=XL(SEARCH);}
            snprintf(line,sizeof(line),"%s %s",e->folder?">":e->favorite?"+":e->audio?"~":"*",name);text_at(line,35,64+(i-start)*28,492,i==entry_index?selected:normal);}
        if(!entry_count)text_at(XL(EMPTY),35,90,492,normal);
    }
    if(first_frame)startup_note("left text rendered");
    clip(NULL);
    if(cover&&(options||playing||panel==3)){SDL_Rect r={576,142,80,112};SDL_RenderCopy(renderer,cover,NULL,&r);}
    side(panel==1?XL(HELP):panel==2?XL(SETTINGS):panel==3?XL(INFO):playing?(paused?XL(PAUSED_TITLE):XL(PLAYING_TITLE)):options?XL(OPTIONS):XL(LIBRARY),0);
    if(fetch.thread){side(XL(LOADING),2);side(XL(CANCEL),4);}
    else{side(XL(SELECT),1);if(!cover||(!options&&!playing&&panel!=3)){side(XL(BACK),3);side(XL(OPEN_SETTINGS),4);side(XL(OPEN_HELP),5);}side(XL(EXIT),8);}
    SDL_Rect footer={27,303,666,38};clip(&footer);text_at(status,32,308,650,normal);clip(NULL);
    receiver();
}
static void menu_draw(void){
    Uint32 began=SDL_GetTicks();
    if(playing&&media_audio&&fullscreen&&!panel){
        /* No hidden theme/text/receiver pass underneath a fullscreen effect. */
        SDL_RenderSetScale(renderer,(float)width/720,(float)height/480);clip(NULL);
        color(0,0,0);SDL_RenderClear(renderer);visual_draw(1);
        if(!visual_mode)text_at(media_name,30,15,660,(SDL_Color){235,235,235,255});
    }else menu_render();
    controls_draw();if(first_frame)startup_note("before first present");
    Uint32 ready=SDL_GetTicks();SDL_RenderPresent(renderer);Uint32 presented=SDL_GetTicks();
    if(first_frame){startup_note("first present complete");first_frame=0;}
    /* Non-stopping timing: the serial debugger must not halt playback to
     * determine whether formulas, presentation or GUI copies are expensive. */
    static Uint32 stats_at;static unsigned frames,compose_ms,present_ms,worst;
    if(playing&&media_audio&&diagnostics_enabled){
        if(!frames)stats_at=began;
        frames++;compose_ms+=ready-began;present_ms+=presented-ready;
        if(presented-began>worst)worst=presented-began;
        if(presented-stats_at>=5000){
            char line[240];snprintf(line,sizeof(line),"visual timing: mode=%d output=%dx%d full=%d frames=%u span_ms=%u compose_ms=%u present_ms=%u worst_ms=%u underruns=%u",visual_mode,width,height,fullscreen,frames,presented-stats_at,compose_ms,present_ms,worst,underflows);
            startup_note(line);frames=compose_ms=present_ms=worst=0;
        }
    }else frames=compose_ms=present_ms=worst=0;
}
static void frame_draw_software(plm_frame_t *f){
    if(!video_texture||texture_w!=(int)f->width||texture_h!=(int)f->height){if(video_texture)SDL_DestroyTexture(video_texture);texture_w=f->width;texture_h=f->height;video_texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_IYUV,SDL_TEXTUREACCESS_STREAMING,texture_w,texture_h);}
    if(!video_texture){stream_error(SDL_GetError());return;}
    SDL_UpdateYUVTexture(video_texture,NULL,f->y.data,f->y.width,f->cb.data,f->cb.width,f->cr.data,f->cr.width);
    if(fullscreen){SDL_RenderSetScale(renderer,1,1);color(0,0,0);SDL_RenderClear(renderer);
        /* Server frames contain a complete 16:9 canvas, including pillarbox
         * for 4:3 sources. SD output pixels are anamorphic on a wide TV. */
        double ratio=(16.0/9.0)*((double)width/height)/(display_wide?16.0/9.0:4.0/3.0);int w=width,h=(int)(w/ratio);if(h>height){h=height;w=(int)(h*ratio);}SDL_Rect dst={(width-w)/2,(height-h)/2,w,h};SDL_RenderCopy(renderer,video_texture,NULL,&dst);video_progress();controls_draw();SDL_RenderPresent(renderer);
    }else{menu_render();SDL_RenderSetScale(renderer,(float)width/720,(float)height/480);color(0,0,0);SDL_Rect panel={27,60,506,232};SDL_RenderFillRect(renderer,&panel);double ratio=(16.0/9.0)*1.5/(display_wide?16.0/9.0:4.0/3.0);int w=506,h=(int)(w/ratio);if(h>232){h=232;w=(int)(h*ratio);}SDL_Rect dst={27+(506-w)/2,60+(232-h)/2,w,h};SDL_RenderCopy(renderer,video_texture,NULL,&dst);controls_draw();SDL_RenderPresent(renderer);}
}
static void frame_draw(plm_frame_t *f){
    static Uint32 gui_at;static int old_full=-1,old_controls=-1,old_row=-1;
    if(overlay_prepare(f->width,f->height)){
        SDL_Rect dst;
        if(fullscreen){
            double ratio=(16.0/9.0)*((double)width/height)/(display_wide?16.0/9.0:4.0/3.0);
            int w=width,h=(int)(w/ratio);if(h>height){h=height;w=(int)(h*ratio);}
            dst=(SDL_Rect){(width-w)/2,(height-h)/2,w,h};
        }else{
            double ratio=(16.0/9.0)*1.5/(display_wide?16.0/9.0:4.0/3.0);
            int w=506,h=(int)(w/ratio);if(h>232){h=232;w=(int)(h*ratio);}
            dst=(SDL_Rect){(27+(506-w)/2)*width/720,(60+(232-h)/2)*height/480,w*width/720,h*height/480};
        }
        /* GUI scanout is independent of video. Update the progress bar at 4 Hz
         * and buttons immediately; don't copy a full RGB screen per frame. */
        Uint32 now=SDL_GetTicks();
        if(!overlay.active||old_full!=fullscreen||old_controls!=controls||old_row!=control_row||now-gui_at>=250){
            Uint32 start=now;
            int full=!overlay.active||old_full!=fullscreen||old_controls!=controls;
            if(full){
                if(fullscreen){SDL_RenderSetScale(renderer,1,1);color(0,0,0);SDL_RenderClear(renderer);}
                else{menu_render();color(0,0,0);SDL_Rect r={27,60,506,232};SDL_RenderFillRect(renderer,&r);}
                SDL_RenderSetScale(renderer,1,1);color(1,2,3);SDL_RenderFillRect(renderer,&dst);
                video_progress();controls_draw();SDL_RenderPresent(renderer);
            }else{
                /* Preserve static pixels in SDL's existing software surface.
                 * Flush commands without SDL_RenderPresent's full-screen copy. */
                SDL_Rect damage[3];int count=0;
                if(fullscreen&&!media_live&&media_duration>0){
                    video_progress();damage[count++]=(SDL_Rect){width/30,height-10,width-2*(width/30),3};
                }
                if(controls){
                    controls_draw();
                    damage[count++]=(SDL_Rect){74*width/720,64*height/480,572*width/720+2,352*height/480+2};
                }else if(!fullscreen){
                    SDL_RenderSetScale(renderer,(float)width/720,(float)height/480);
                    SDL_Rect footer={27,303,666,38},instruments={27,360,666,110},all={0,0,720,480};
                    clip(&footer);SDL_RenderCopy(renderer,skin,NULL,&all);
                    text_at(status,32,308,650,(SDL_Color){225,234,237,255});clip(NULL);
                    clip(&instruments);SDL_RenderCopy(renderer,skin,NULL,&all);receiver();clip(NULL);
                    damage[count++]=(SDL_Rect){26*width/720,302*height/480,668*width/720+2,40*height/480+2};
                    damage[count++]=(SDL_Rect){26*width/720,359*height/480,668*width/720+2,112*height/480+2};
                }
                SDL_RenderFlush(renderer);
                if(count)SDL_UpdateWindowSurfaceRects(window,damage,count);
            }
            overlay.gui_ms+=SDL_GetTicks()-start;gui_at=now;
            old_full=fullscreen;old_controls=controls;old_row=control_row;
        }
        if(!overlay.shown&&!overlay.busy){char line[200];snprintf(line,sizeof(line),"PVIDEO init enable=%08x inherited=%08x reset=%08x output=%dx%d field_lines=%d source=%ux%u",overlay.initial_enable,overlay.initial_buffer,overlay.reset_buffer,width,height,height==1080?540:height,f->width,f->height);startup_note(line);}
        if(overlay_present(f,dst,height)>=0)return;
        {char line[180];snprintf(line,sizeof(line),"video: NV2A busy timeout buffer=%08x shown=%u; using software",overlay.last_buffer,overlay.shown);startup_note(line);}
    }
    frame_draw_software(f);
}
static void button_down(int button){
    if(panel==7){
        if(display_confirm){
            if(button==SDL_CONTROLLER_BUTTON_A){VIDEO_MODE m=XVideoGetMode();output_selected_w=m.width;output_selected_h=m.height;output_selected_hz=m.refresh;output_height=m.height;display_confirm=0;preferences(1);}
            else if(button==SDL_CONTROLLER_BUTTON_B){display_confirm=0;if(!display_switch(display_previous)){quit=1;return;}}
        }else if(button==SDL_CONTROLLER_BUTTON_B){panel=2;panel_row=14;}
        else if(display_count){
            if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)panel_row=(panel_row+display_count-1)%display_count;
            if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)panel_row=(panel_row+1)%display_count;
            if(button==SDL_CONTROLLER_BUTTON_A){display_previous=XVideoGetMode();if(!display_switch(display_modes[panel_row])){if(!display_switch(display_previous)){quit=1;return;}snprintf(status,sizeof(status),XL(DISPLAY_RESTORED));}else{display_confirm=1;display_deadline=SDL_GetTicks()+15000;}}
        }return;
    }
    /* Back must also work while a catalog request or playback is active. */
    if(button==SDL_CONTROLLER_BUTTON_BACK){quit=1;return;}
    if(visual_panel_input(button)||visual_input(button))return;
    if(next_pending){if(button==SDL_CONTROLLER_BUTTON_B){next_pending=0;snprintf(status,sizeof(status),XL(AUTOPLAY_CANCELLED));}else if(button==SDL_CONTROLLER_BUTTON_A){next_pending=0;next_media(0);}return;}
    if(retry_pending){if(button==SDL_CONTROLLER_BUTTON_B){retry_pending=0;snprintf(status,sizeof(status),XL(RETRY_CANCELLED));}return;}
    if(fetch.thread&&fetch.type!=3){if(button==SDL_CONTROLLER_BUTTON_B){fetch_stop();autoplay_pending=remote_play_pending=0;snprintf(status,sizeof(status),XL(CANCELLED));}return;}
    if(controls){
        if(button==SDL_CONTROLLER_BUTTON_B||button==SDL_CONTROLLER_BUTTON_START||button==SDL_CONTROLLER_BUTTON_X){controls=0;return;}
        if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)control_row=(control_row+7)%8;
        if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)control_row=(control_row+1)%8;
        if(button==SDL_CONTROLLER_BUTTON_A){
            switch(control_row){
            case 0:if(!paused)report_playback("paused");player_pause();report_time=0;break;
            case 1:seek_playback(player_position()-30);break;case 2:seek_playback(player_position()+30);break;
            case 3:chapter_jump(0);break;case 4:chapter_jump(1);break;
            case 5:controls=0;next_media(1);break;case 6:controls=0;next_media(0);break;
            case 7:controls=0;button_down(SDL_CONTROLLER_BUTTON_B);break;
            }
        }return;
    }
    if(panel){
        if(panel==8){
            if(button==SDL_CONTROLLER_BUTTON_B){panel=0;return;}
            if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)panel_row=(panel_row+7)%8;
            if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)panel_row=(panel_row+1)%8;
            if(button==SDL_CONTROLLER_BUTTON_A){if(panel_row>=2&&panel_row<=4&&!in_queue())return;library_action(panel_row);}return;
        }
        if(panel==6){
            if(button==SDL_CONTROLLER_BUTTON_B){memset(keyboard_draft,0,sizeof(keyboard_draft));panel=keyboard_field==3?0:5;return;}
            if(button==SDL_CONTROLLER_BUTTON_START){if(keyboard_field==3){if(strlen(keyboard_draft)<2){snprintf(status,sizeof(status),XL(SEARCH_SHORT));return;}snprintf(search_query,sizeof(search_query),"%s",keyboard_draft);panel=0;options=0;browse(":xbox:search:",0);}else{keyboard_accept();panel=5;}return;}
            if(button==SDL_CONTROLLER_BUTTON_X){int n=strlen(keyboard_draft);if(n){do{n--;}while(n>0&&(keyboard_draft[n]&0xC0)==0x80);keyboard_draft[n]=0;}}
            if(button==SDL_CONTROLLER_BUTTON_Y)*keyboard_draft=0;
            if(button==SDL_CONTROLLER_BUTTON_A)keyboard_insert();
            int delta=button==SDL_CONTROLLER_BUTTON_DPAD_UP?-12:button==SDL_CONTROLLER_BUTTON_DPAD_DOWN?12:button==SDL_CONTROLLER_BUTTON_DPAD_LEFT?-1:button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT?1:0;
            keyboard_key=(keyboard_key+102+delta)%102;return;
        }
        if(panel==5){
            if(button==SDL_CONTROLLER_BUTTON_B){panel=0;memset(server_draft_password,0,sizeof(server_draft_password));return;}
            if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)panel_row=(panel_row+4)%5;
            if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)panel_row=(panel_row+1)%5;
            if(button==SDL_CONTROLLER_BUTTON_A){
                if(panel_row<3){keyboard_begin(panel_row);panel=6;snprintf(status,sizeof(status),XL(KEYBOARD_HELP));}
                else if(!server_draft_valid())snprintf(status,sizeof(status),XL(INVALID_SERVER));
                else if(panel_row==3){
                    strcpy(config_old_host,host);strcpy(config_old_password,password);config_old_port=port;
                    strcpy(host,server_draft_host);strcpy(password,server_draft_password);port=(unsigned)strtoul(server_draft_port,NULL,10);
                    fetch_start("/api/health",4);config_testing=1;
                }else if(server_save()){
                    panel=0;remote_sequence=0;report_started=0;*media_id=0;options=0;root_index=0;*folder_path=0;entry_count=0;
                    memset(server_draft_password,0,sizeof(server_draft_password));browse("",0);
                }else snprintf(status,sizeof(status),XL(SAVE_FAILED));
            }return;
        }
        if(button==SDL_CONTROLLER_BUTTON_B||button==SDL_CONTROLLER_BUTTON_Y){if(panel==2||panel==4)preferences(1);panel=0;return;}
        if(panel==4){
            if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)panel_row=(panel_row+6)%7;
            if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)panel_row=(panel_row+1)%7;
            int d=button==SDL_CONTROLLER_BUTTON_DPAD_LEFT?-1:1;
            if(button==SDL_CONTROLLER_BUTTON_A||button==SDL_CONTROLLER_BUTTON_DPAD_LEFT||button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT){
                if(panel_row==0){spectrum_analysis_mode=!spectrum_analysis_mode;spectrum_analysis_reset();}
                if(panel_row==1)spectrum_band_count=spectrum_band_choice(spectrum_band_count,d);
                if(panel_row==2)spectrum_gain_db=-24+(spectrum_gain_db+24+49+d)%49;
                if(panel_row==3)spectrum_style=(spectrum_style+5+d)%5;
                if(panel_row==4)spectrum_segments=!spectrum_segments;
                if(panel_row==5)spectrum_led_count=8+(spectrum_led_count-8+25+d)%25;
                if(panel_row==6)spectrum_peak_hold=!spectrum_peak_hold;
            }return;
        }
        if(panel==2){
            if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)panel_row=(panel_row+18)%19;
            if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)panel_row=(panel_row+1)%19;
            int d=button==SDL_CONTROLLER_BUTTON_DPAD_LEFT?-1:1;
            if(button==SDL_CONTROLLER_BUTTON_A||button==SDL_CONTROLLER_BUTTON_DPAD_LEFT||button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT){
                switch(panel_row){case 0:auto_next=!auto_next;break;case 1:repeat_one=!repeat_one;break;case 2:shuffle_music=!shuffle_music;break;
                case 3:show_spectrum=!show_spectrum;break;case 4:quality=(quality+6+d)%6;break;
                case 5:volume+=d*5;if(volume<0)volume=0;if(volume>100)volume=100;break;
                case 6:prefer_audio=(prefer_audio+7+d)%7;break;case 7:prefer_subtitle=(prefer_subtitle+7+d)%7;break;
                case 8:server_editor();break;case 9:panel=PANEL_VISUAL;panel_row=0;break;
                case 10:diagnostics_enabled=!diagnostics_enabled;if(!diagnostics_enabled)*player_diagnostic=0;break;
                case 11:next_delay=(next_delay+31+d)%31;break;
                case 12:video_codec=!video_codec;break;case 13:display_wide=!display_wide;break;
                case 14:if(!playing){display_list();panel=7;panel_row=0;}break;
                case 15:audio_matrix=(audio_matrix+3+d)%3;break;
                case 16:video_hardware=!video_hardware;break;
                case 17:ui_language=(ui_language+3+d)%3;text_cache_clear();preferences(1);break;
                case 18:audio_quality=(audio_quality+5+d)%5;break;}
            }
        }return;
    }
    if(playing){
        if(button==SDL_CONTROLLER_BUTTON_B){report_playback("stopped");report_started=0;start_position=player_position();player_stop();preferences(1);snprintf(status,sizeof(status),XL(STOPPED));}
        else if(button==SDL_CONTROLLER_BUTTON_START||button==SDL_CONTROLLER_BUTTON_X){
            if(button==SDL_CONTROLLER_BUTTON_X&&media_audio){panel=PANEL_VISUAL;panel_row=0;}else{controls=1;control_row=0;}
        }
        else if(button==SDL_CONTROLLER_BUTTON_A){if(!paused)report_playback("paused");player_pause();report_time=0;}
        else if(button==SDL_CONTROLLER_BUTTON_LEFTSHOULDER)next_media(1);
        else if(button==SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)next_media(0);
        else if(button==SDL_CONTROLLER_BUTTON_Y){if(media_audio&&!show_spectrum)return;fullscreen=!fullscreen;if(paused&&next_frame)frame_draw(next_frame);}
        else if(button==SDL_CONTROLLER_BUTTON_DPAD_UP){volume+=5;if(volume>100)volume=100;}
        else if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN){volume-=5;if(volume<0)volume=0;}
        else if(!media_live&&(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT||button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)){double p=player_position()+(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT?-30:30);if(p<0)p=0;if(media_duration>1&&p>=media_duration)p=media_duration-1;player_start(p);report_time=0;}
        return;
    }
    if(options){
        int rows=media_audio?1:4;
        if(button==SDL_CONTROLLER_BUTTON_B){options=0;return;}
        if(button==SDL_CONTROLLER_BUTTON_Y){panel=3;return;}
        if(button==SDL_CONTROLLER_BUTTON_X){start_position=start_position>0?0:media_resume;return;}
        if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)option_row=(option_row+rows-1)%rows;
        if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)option_row=(option_row+1)%rows;
        int d=button==SDL_CONTROLLER_BUTTON_DPAD_LEFT?-1:1;
        if(button==SDL_CONTROLLER_BUTTON_A||button==SDL_CONTROLLER_BUTTON_DPAD_LEFT||button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT){
            if(!option_row&&button==SDL_CONTROLLER_BUTTON_A){if(fetch.thread)fetch_stop();begin_playback(start_position);}
            else if(option_row==1&&audio_count)audio_track=(audio_track+audio_count+d)%audio_count;
            else if(option_row==2){subtitle_track=(subtitle_track+1+subtitle_count+1+d)%(subtitle_count+1)-1;}
            else if(option_row==3)quality=(quality+6+d)%6;
        }return;
    }
    if(button==SDL_CONTROLLER_BUTTON_LEFTSHOULDER){panel=8;panel_row=0;snprintf(status,sizeof(status),XL(ACTIONS_HELP));}
    else if(button==SDL_CONTROLLER_BUTTON_RIGHTSHOULDER){keyboard_begin(3);panel=6;snprintf(status,sizeof(status),XL(KEYBOARD_HELP));}
    else if(button==SDL_CONTROLLER_BUTTON_B&&has_parent)browse(parent_path,0);
    else if(button==SDL_CONTROLLER_BUTTON_X){panel=2;panel_row=0;}
    else if(button==SDL_CONTROLLER_BUTTON_Y)panel=1;
    else if(button==SDL_CONTROLLER_BUTTON_START)browse(folder_path,page_offset);
    else if(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT&&page_offset>=64)browse(folder_path,page_offset-64);
    else if(button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT&&page_offset+64<total_entries)browse(folder_path,page_offset+64);
    else if(button==SDL_CONTROLLER_BUTTON_DPAD_UP&&entry_count)entry_index=(entry_index+entry_count-1)%entry_count;
    else if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN&&entry_count)entry_index=(entry_index+1)%entry_count;
    else if(button==SDL_CONTROLLER_BUTTON_A&&entry_count){Entry *e=&entries[entry_index];root_index=e->root;if(!strcmp(e->target,":xbox:search:")){keyboard_begin(3);panel=6;snprintf(status,sizeof(status),XL(KEYBOARD_HELP));}else if(e->folder)browse(e->target,0);else select_media(e);}
}
static void remote_execute(void){
    if(!parse_json(remote_body))return;
    unsigned sequence=number(0,"sequence");if(!sequence||sequence<=remote_sequence)return;
    remote_sequence=sequence;char action[24];text_tok(field(0,"action"),action,sizeof(action));
    if(!strcmp(action,"play")){
        next_pending=0;
        Entry item={0};if(!text_tok(field(0,"id"),item.target,sizeof(item.target))||!*item.target)return;
        remote_audio=number(0,"audio");remote_subtitle=number(0,"subtitle");remote_start=number(0,"start");
        char size[24];if(text_tok(field(0,"xbox_size"),size,sizeof(size)))
            for(unsigned i=0;i<sizeof(quality_keys)/sizeof(*quality_keys);i++)if(!strcmp(size,quality_keys[i])){quality=i;break;}
        if(text_tok(field(0,"xbox_audio"),size,sizeof(size)))for(int i=0;i<5;i++)if(!strcmp(size,audio_quality_keys[i]))audio_quality=i;
        report_playback("stopped");report_started=0;player_stop();fetch_stop();panel=controls=0;autoplay_pending=0;
        remote_play_pending=1;snprintf(item.name,sizeof(item.name),"Remote selection");select_media(&item);
    }else if(!strcmp(action,"stop")){
        next_pending=0;
        remote_play_pending=autoplay_pending=retry_pending=controls=panel=0;fetch_stop();report_playback("stopped");report_started=0;player_stop();
    }else if(playing){
        if(!strcmp(action,"pause")&&!paused){report_playback("paused");player_pause();}
        else if(!strcmp(action,"resume")&&paused){player_pause();report_time=0;}
        else if(!strcmp(action,"seek"))seek_playback(number(0,"seconds"));
        else if(!strcmp(action,"next"))next_media(0);
        else if(!strcmp(action,"previous"))next_media(1);
    }
}
int main(void){
    FILE *boot=fopen("D:\\xbox-player.log","w");if(boot){fputs("Xbox player 0.6.7 Monkey / MilkDrop / NV2A offscreen / O3 LTO\n",boot);fclose(boot);}
    /* XC_LANGUAGE: https://xboxdevwiki.net/EEPROM (read-only). */
    ULONG language_type=0,dashboard_language=1;
    if(ExQueryNonVolatileSetting(XC_LANGUAGE,&language_type,&dashboard_language,sizeof(dashboard_language),NULL)>=0)dashboard_german=dashboard_language==3;
    spectrum_analysis_mode=1;preferences(0);snprintf(status,sizeof(status),"%s",XL(CONNECTING));snprintf(report_client,sizeof(report_client),"xbox-%08lx-%08lx",(unsigned long)GetTickCount(),(unsigned long)KeQueryPerformanceCounter());
    startup_note("entry: before graphics/input initialization");
    int configured=config();display_list();
    /* With 480p enabled nxdk exposes 720x480, not 640x480. Choose an actual
     * enumerated default instead of continuing after a failed fixed mode. */
    VIDEO_MODE boot_mode;void *boot_cursor=NULL;
    if(!XVideoListModes(&boot_mode,32,0,&boot_cursor)){debugPrint("No supported video output\n");XReboot();return 1;}
    if(output_selected_w){for(int i=0;i<display_count;i++){VIDEO_MODE m=display_modes[i];if(m.width==output_selected_w&&m.height==output_selected_h&&m.refresh==output_selected_hz){boot_mode=m;break;}}}
    else if(output_height!=480){for(int i=0;i<display_count;i++){VIDEO_MODE m=display_modes[i];if(m.height==output_height){boot_mode=m;break;}}}
    if(!XVideoSetMode(boot_mode.width,boot_mode.height,32,boot_mode.refresh)){debugPrint("Video output failed\n");XReboot();return 1;}
    VIDEO_MODE mode=XVideoGetMode();width=mode.width;height=mode.height;
    if(SDL_Init(SDL_INIT_GAMECONTROLLER)){debugPrint("Controller initialization failed\n");for(;;)Sleep(1000);}
    SDL_SetAssertionHandler(assertion_log,NULL);
    /* Establish input before allocating the graphical UI and before starting
     * concurrent catalog work. Queued DEVICEADDED events are deduplicated.
     * No SDL timer callbacks are used: GetTicks/Delay do not need TIMER init. */
    for(int i=0;i<SDL_NumJoysticks();i++)if(SDL_IsGameController(i))controller_open(i);
    if(SDL_InitSubSystem(SDL_INIT_VIDEO)||TTF_Init()){debugPrint("SDL/TTF startup failed\n");for(;;)Sleep(1000);}
    SDL_SetHint(SDL_HINT_RENDER_BATCHING,"1");
    window=SDL_CreateWindow("PSPStreamer Xbox",0,0,width,height,SDL_WINDOW_SHOWN);renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    font=TTF_OpenFont("D:\\font.ttf",18);SDL_Surface *image=IMG_Load("D:\\theme.png");if(image){skin=SDL_CreateTextureFromSurface(renderer,image);SDL_FreeSurface(image);}
    if(!renderer||!font||!skin){debugPrint("Missing font.ttf/theme.png or renderer: %s\n",SDL_GetError());for(;;)Sleep(1000);}
    FILE *log=fopen("D:\\xbox-player.log","a");if(log){MM_STATISTICS memory={0};memory.Length=sizeof(memory);MmQueryStatistics(&memory);fprintf(log,"RAM pages=%lu output=%dx%d\n",(unsigned long)memory.TotalPhysicalPages,width,height);fclose(log);}
    menu_draw();startup_note("network initialization");int network=nxNetInit(NULL);connection_ready=configured;startup_note("network initialization returned");if(configured&&!network)browse("",0);else snprintf(status,sizeof(status),XL(SERVER_SETUP),network);
    startup_note("main: entering event loop");
    SDL_Event event;Uint32 draw=0,repeat=0;int held=-1,first_poll=1,first_loop=1,trigger_held[2]={0,0};
    while(!quit){
        if(display_confirm&&(Sint32)(SDL_GetTicks()-display_deadline)>=0){display_confirm=0;if(!display_switch(display_previous)){quit=1;break;}}
        if(first_poll)startup_note("main: before first event poll");
        while(SDL_PollEvent(&event)){
            if(event.type==SDL_CONTROLLERDEVICEADDED){controller_open(event.cdevice.which);}
            else if(event.type==SDL_CONTROLLERDEVICEREMOVED){held=-1;for(int i=0;i<4;i++)if(pads[i]&&SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i]))==event.cdevice.which){SDL_GameControllerClose(pads[i]);pads[i]=NULL;}}
            else if(event.type==SDL_CONTROLLERBUTTONDOWN){button_down(event.cbutton.button);if(event.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_UP||event.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_DOWN){held=event.cbutton.button;repeat=SDL_GetTicks()+400;}}
            else if(event.type==SDL_CONTROLLERBUTTONUP&&held==event.cbutton.button)held=-1;
            else if(event.type==SDL_CONTROLLERAXISMOTION&&(event.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERLEFT||event.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERRIGHT)){
                int dir=event.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERRIGHT,down=event.caxis.value>16000;
                if(down&&!trigger_held[dir]&&playing&&!media_live&&!(media_audio&&visual_mode==1))chapter_jump(dir);
                trigger_held[dir]=down;
            }
        }
        if(first_poll){startup_note("main: first event poll complete");first_poll=0;}
        if(held>=0&&(Sint32)(SDL_GetTicks()-repeat)>=0){button_down(held);repeat=SDL_GetTicks()+100;}
        if(first_loop)startup_note("main: before worker done check");
        if(fetch.thread&&SDL_AtomicGet(&fetch.done)){
            startup_note("catalog: joining completed worker");
            SDL_WaitThread(fetch.thread,NULL);fetch.thread=NULL;
            startup_note("catalog: parsing response");
            char *body=fetch.body;fetch.body=NULL;int type=fetch.type,want_art=0;Entry following={0};
            if(type==4){config_restore_test();snprintf(status,sizeof(status),"%s",*fetch.error?fetch.error:XL(SERVER_OK));}
            else if(type==3){fetch.body=body;if(!*fetch.error)artwork_read();fetch.body=NULL;snprintf(status,sizeof(status),XL(OPTIONS_HINT));}
            else if(*fetch.error){autoplay_pending=remote_play_pending=0;snprintf(status,sizeof(status),"%s",fetch.error);}
            else if(type==5){panel=0;options=0;browse(folder_path,page_offset);}
            else if(type==2){
                if(parse_json(body)){text_tok(field(0,"id"),following.target,sizeof(following.target));text_tok(field(0,"name"),following.name,sizeof(following.name));char kind[16];text_tok(field(0,"kind"),kind,sizeof(kind));following.audio=!strcmp(kind,"audio");queued_audio=field(0,"audio")>=0?number(0,"audio"):-1;queued_subtitle=field(0,"subtitle")>=0?number(0,"subtitle"):-2;}
                if(!*following.target)snprintf(status,sizeof(status),XL(FOLDER_END));
            }else if(!(type?parse_metadata(body):parse_catalog(body))){autoplay_pending=remote_play_pending=0;snprintf(status,sizeof(status),XL(INVALID_REPLY));}
            else{options=type;option_row=0;snprintf(status,sizeof(status),options?XL(OPTIONS_HINT):XL(LIST_HINT),total_entries);
                if(type==0){search_refresh_at=SDL_GetTicks()+2000;if(!strcmp(folder_path,":xbox:search:"))snprintf(status,sizeof(status),XL(SEARCH_STATE),total_entries,search_running,search_limited,search_errors);}
                if(type==1){apply_metadata_preferences();want_art=!strncmp(media_id,"plex.",5)||!strncmp(media_id,"jellyfin.",9)||!strncmp(media_id,"dlna.",5);
                    if(remote_play_pending){
                        remote_play_pending=0;autoplay_pending=0;
                        audio_track=remote_audio>=0&&remote_audio<audio_count?remote_audio:0;
                        subtitle_track=remote_subtitle>=0&&remote_subtitle<subtitle_count?remote_subtitle:-1;
                        begin_playback(remote_start);want_art=0;
                    }
                    if(autoplay_pending){autoplay_pending=0;begin_playback(0);want_art=0;}}
            }
            free(body);
            if(*following.target){autoplay_pending=1;select_media(&following);}
            else if(want_art){char encoded[4700],request[6000];url_encode(media_id,encoded,sizeof(encoded));snprintf(request,sizeof(request),"/api/xbox/artwork?item=%s",encoded);fetch_start(request,3);}
            startup_note(status);
        }
        if(first_loop)startup_note("main: worker done check returned");
        if(retry_pending&&(Sint32)(SDL_GetTicks()-retry_at)>=0){retry_pending=0;if(!player_start(retry_position))snprintf(status,sizeof(status),XL(RETRY_FAILED));}
        if(playing){int result=player_tick();
            if(paused&&report_started&&SDL_GetTicks()-report_time>=15000)report_playback("paused");
            if(!paused&&(audio_running||rendered)&&(!report_started||SDL_GetTicks()-report_time>=15000)){report_started=1;report_playback("playing");}
            if(result==1){frame_draw(next_frame);next_frame=NULL;}
            else if(result<0||result==2){
                char end_note[230];snprintf(end_note,sizeof(end_note),"playback end result=%d clean=%d pictures=%u/%u codec=MPEG-%d size=%s error=%s",result,stream.ended,video.displayed,video.submitted,video_codec?2:1,quality_keys[quality],stream.error);startup_note(end_note);
                report_playback("stopped");report_started=0;start_position=player_position();
                int retry=result<0&&(strstr(stream.error,"network")||strstr(stream.error,"interrupted")||strstr(stream.error,"Truncated"));
                snprintf(status,sizeof(status),"%s",result==2?XL(FINISHED):*stream.error?stream.error:XL(STREAM_FAILED));player_stop();
                if(result==2&&!media_live){start_position=0;if(repeat_one||auto_next){if(next_delay){next_pending=1;next_at=SDL_GetTicks()+next_delay*1000;}else next_media(0);}}
                else if(retry&&retry_count<3){retry_count++;retry_pending=1;retry_position=media_live?0:start_position;retry_at=SDL_GetTicks()+2000;snprintf(status,sizeof(status),XL(RETRY),retry_count);}
            }
        }
        report_tick();
        if(search_running&&!playing&&!options&&!panel&&!fetch.thread&&!strcmp(folder_path,":xbox:search:")&&(Sint32)(SDL_GetTicks()-search_refresh_at)>=0)browse(folder_path,page_offset);
        if(next_pending){int left=(Sint32)(next_at-SDL_GetTicks());if(left<=0){next_pending=0;next_media(0);}else snprintf(status,sizeof(status),XL(NEXT_COUNTDOWN),(left+999)/1000);}
        if(connection_ready&&!network&&panel!=5&&panel!=6&&panel!=7&&remote_poll())remote_execute();
        if(first_loop)startup_note("main: before clock/redraw");
        if((!playing||media_audio||paused)&&SDL_GetTicks()-draw>=50){menu_draw();draw=SDL_GetTicks();}
        if(first_loop)startup_note("main: before yield");
        SDL_Delay(1);
        if(first_loop){startup_note("main: first loop complete");first_loop=0;}
    }
    report_playback("stopped");remote_shutdown();fetch_stop();player_stop();report_shutdown();preferences(1);if(cover)SDL_DestroyTexture(cover);if(backdrop)SDL_DestroyTexture(backdrop);if(audio_initialized)XAudioPause();if(pcm)MmFreeContiguousMemory(pcm);
    if(spectrum_texture)SDL_DestroyTexture(spectrum_texture);
    for(int i=0;i<4;i++)if(pads[i])SDL_GameControllerClose(pads[i]);nxNetShutdown();text_cache_clear();TTF_CloseFont(font);SDL_DestroyTexture(skin);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);TTF_Quit();SDL_Quit();XReboot();return 0;
}

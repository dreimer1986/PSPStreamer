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
#include "net.h"
#include "catalog.h"
#include "player.h"
#include "settings.h"
#include "report.h"
#include "render_clip.h"

static void menu_draw(void);

static SDL_Window *window;static SDL_Renderer *renderer;static TTF_Font *font;static SDL_Texture *skin;
static SDL_GameController *pads[4];
static int width=640,height=480,options,fullscreen=1,quit;
static char status[160]="Connecting...";static float meters[2];
static int first_frame=1;
static int panel,panel_row,autoplay_pending,media_live,retry_count,retry_pending;
static double start_position,retry_position;static Uint32 retry_at;
static SDL_Texture *cover,*backdrop;
static char carry_audio[16],carry_subtitle[16];static int carry_tracks,carry_subtitle_off;
static void startup_note(const char *stage){
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
typedef struct {SDL_Thread *thread;SDL_atomic_t cancel,done;char path[6000],error[100];char *body;unsigned size;int type;} Fetch;
static Fetch fetch;
static int fetch_worker(void *unused){
    (void)unused;Http h;
    startup_note("catalog: worker entered, opening HTTP");
    if(!http_request(&h,fetch.path,&fetch.cancel,NULL,fetch.type==3?15000:180000)){snprintf(fetch.error,sizeof(fetch.error),"HTTP %d / connection failed",h.status);goto done;}
    startup_note("catalog: HTTP headers received");
    fetch.body=malloc(512*1024);if(!fetch.body){snprintf(fetch.error,sizeof(fetch.error),"Out of JSON memory");http_close(&h);goto done;}
    unsigned at=0;int n=0;
    while(at<512*1024-1&&(n=http_read(&h,fetch.body+at,512*1024-1-at))>0)at+=n;
    fetch.body[at]=0;fetch.size=at;http_close(&h);
    if(n<0||at==512*1024-1)snprintf(fetch.error,sizeof(fetch.error),"Incomplete/oversized library reply");
done:startup_note(*fetch.error?fetch.error:"catalog: response complete");SDL_AtomicSet(&fetch.done,1);return 0;
}
static void fetch_stop(void){if(fetch.thread){SDL_AtomicSet(&fetch.cancel,1);SDL_WaitThread(fetch.thread,NULL);fetch.thread=NULL;}free(fetch.body);fetch.body=NULL;}
static void fetch_start(const char *path,int type){
    startup_note("catalog: request queued");
    fetch_stop();memset(&fetch,0,sizeof(fetch));fetch.type=type;snprintf(fetch.path,sizeof(fetch.path),"%s",path);
    snprintf(status,sizeof(status),"Loading... B cancels");
    startup_note("catalog: drawing loading status");menu_draw();
    startup_note("catalog: creating worker");
    fetch.thread=SDL_CreateThreadWithStackSize(fetch_worker,"catalog",65536,NULL);
    startup_note(fetch.thread?"catalog: worker created":"catalog: worker creation failed");
    if(!fetch.thread)snprintf(status,sizeof(status),"Cannot start network worker");
}
static void browse(const char *path,int offset){char encoded[4700],request[6000];if(!url_encode(path,encoded,sizeof(encoded))){snprintf(status,sizeof(status),"Folder path too long");return;}snprintf(request,sizeof(request),"/api/xbox/library?root=%d&path=%s&offset=%d",root_index,encoded,offset);fetch_start(request,0);}
static void select_media(Entry *e){
    if(!autoplay_pending)carry_tracks=0;
    if(cover)SDL_DestroyTexture(cover);cover=NULL;if(backdrop)SDL_DestroyTexture(backdrop);backdrop=NULL;
    snprintf(media_id,sizeof(media_id),"%s",e->target);snprintf(media_name,sizeof(media_name),"%s",e->name);media_audio=e->audio;
    media_live=!strncmp(media_id,"radio.",6);retry_count=retry_pending=0;
    char encoded[4700],request[6000];url_encode(media_id,encoded,sizeof(encoded));snprintf(request,sizeof(request),"/api/xbox/metadata/%s",encoded);fetch_start(request,1);
}
static void next_media(int previous){
    if(media_live)return;
    carry_tracks=1;snprintf(carry_audio,sizeof(carry_audio),"%s",audio_count?audio_languages[audio_track]:"");
    carry_subtitle_off=subtitle_track<0;snprintf(carry_subtitle,sizeof(carry_subtitle),"%s",subtitle_track>=0?subtitle_languages[subtitle_track]:"");
    report_playback("stopped");report_started=0;player_stop();retry_pending=0;
    char encoded[4700],request[6000];url_encode(media_id,encoded,sizeof(encoded));
    snprintf(request,sizeof(request),"/api/media-next/%s?folder=1&direction=%s&shuffle=%d",encoded,previous?"previous":"next",media_audio&&shuffle_music&&!previous);
    fetch_start(request,2);
}
static void begin_playback(double position){
    start_position=position;report_started=0;retry_count=retry_pending=0;panel=0;fullscreen=!media_audio;
    audio_analysis=media_audio&&show_spectrum;
    preferences(1);
    if(!player_start(position))snprintf(status,sizeof(status),"Cannot start player");else snprintf(status,sizeof(status),"Buffering... B: Stop");
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
    start_position=media_resume;
}
static void artwork_read(void){
    if(fetch.size<20||memcmp(fetch.body,"PSPA",4))return;
    uint16_t dims[4];uint32_t sizes[2];memcpy(dims,fetch.body+4,8);memcpy(sizes,fetch.body+12,8);
    if(dims[0]!=320||dims[1]!=180||dims[2]!=80||dims[3]!=112||
        (sizes[0]&&sizes[0]!=320*180*2)||(sizes[1]&&sizes[1]!=80*112*2)||20+sizes[0]+sizes[1]!=fetch.size)return;
    unsigned at=20;for(int i=0;i<2;i++){if(sizes[i]){
        SDL_Surface *s=SDL_CreateRGBSurfaceFrom(fetch.body+at,dims[i*2],dims[i*2+1],16,dims[i*2]*2,0xf800,0x07e0,0x001f,0);
        if(s){SDL_Texture *t=SDL_CreateTextureFromSurface(renderer,s);SDL_FreeSurface(s);if(i)cover=t;else if(t){backdrop=t;SDL_SetTextureBlendMode(t,SDL_BLENDMODE_BLEND);SDL_SetTextureAlphaMod(t,70);}}
    }at+=sizes[i];}
}
static void color(int r,int g,int b){SDL_SetRenderDrawColor(renderer,r,g,b,255);}
static void text_at(const char *s,int x,int y,int max_width,SDL_Color color){
    if(!*s)return;SDL_Surface *surface=TTF_RenderUTF8_Blended(font,s,color);if(!surface)return;
    SDL_Texture *texture=SDL_CreateTextureFromSurface(renderer,surface);
    SDL_Rect src={0,0,surface->w>max_width?max_width:surface->w,surface->h},dst={x,y,src.w,src.h};
    if(texture){SDL_RenderCopy(renderer,texture,&src,&dst);SDL_DestroyTexture(texture);}SDL_FreeSurface(surface);
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
static void spectrum_draw(int full){
    static float shown[24];unsigned idx=ac97[0x114]&31;
    int x=full?30:37,y=full?40:147,w=full?660:488,h=full?385:135;
    for(int i=0;i<24;i++){float target=audio_running&&!paused&&!underrun?audio_spectrum[idx][i]:0;shown[i]+=(target-shown[i])*(target>shown[i]?.65f:.18f);
        int size=(int)(shown[i]*h);if(size<0)size=0;if(size>h)size=h;
        color(80+i*7,220-i*5,210-i*4);SDL_Rect r={x+i*w/24,y+h-size,w/24-3,size};SDL_RenderFillRect(renderer,&r);
    }
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
    if(panel==1){
        const char *help[]={"A: Open / Play / Pause; B: Back / Stop", "D-pad: Browse; Left/Right in video: seek 30s", "Up/Down in playback: Volume; Y: Fullscreen", "LB / RB: Previous / next file (same folder)", "Triggers: Previous / next chapter", "Menu X: Settings; Y: Help; options Y: Info", "Options X: Start / Resume; Back: Dashboard", "Only clean playback end advances automatically"};
        for(int i=0;i<8;i++)text_at(help[i],35,64+i*28,492,normal);
    }else if(panel==2){
        char lines[8][140];snprintf(lines[0],140,"Autoplay next: %s",auto_next?"On":"Off");snprintf(lines[1],140,"Repeat current: %s",repeat_one?"On":"Off");
        snprintf(lines[2],140,"Folder shuffle (music): %s",shuffle_music?"On":"Off");snprintf(lines[3],140,"Music spectrum: %s",show_spectrum?"On":"Off");
        snprintf(lines[4],140,"Video quality: %s",quality?"640x360":"480x272");snprintf(lines[5],140,"Volume: %d%%",volume);
        snprintf(lines[6],140,"Audio: %s",language_names[prefer_audio]);snprintf(lines[7],140,"Subtitles: %s",prefer_subtitle?language_names[prefer_subtitle]:"Off");
        for(int i=0;i<8;i++)text_at(lines[i],35,64+i*28,492,i==panel_row?selected:normal);
    }else if(panel==3){
        text_at(media_name,35,66,492,selected);char line[160];snprintf(line,sizeof(line),"%d:%02d | %d audio / %d subtitle tracks",(int)media_duration/60,(int)media_duration%60,audio_count,subtitle_count);text_at(line,35,96,492,normal);
        if(*media_artist)text_at(media_artist,35,123,492,normal);if(*media_album)text_at(media_album,35,150,492,normal);
        wrapped(*media_summary?media_summary:"No synopsis supplied by this source.",177,4);
    }else if(playing){
        text_at(media_name,35,70,492,selected);char line[120];double p=player_position();
        snprintf(line,sizeof(line),"%s  %02d:%02d / %02d:%02d",paused?"Paused":"Playing",(int)p/60,(int)p%60,(int)media_duration/60,(int)media_duration%60);text_at(line,35,105,492,normal);
        if(media_audio&&show_spectrum)spectrum_draw(0);
        else{text_at("A: Pause / Resume   B: Stop",35,160,492,normal);text_at("Left / Right: -/+30s   Up / Down: Volume",35,190,492,normal);}
    }else if(options){
        text_at(media_name,35,66,492,selected);
        char lines[4][180];snprintf(lines[0],180,start_position>0?"Resume at %d:%02d (X: from start)":"Play",(int)start_position/60,(int)start_position%60);snprintf(lines[1],180,"Audio: %s",audio_count?audio_labels[audio_track]:"No audio");
        snprintf(lines[2],180,"Subtitles: %s",subtitle_track<0?"Off":subtitle_labels[subtitle_track]);
        snprintf(lines[3],180,"Video: %s",quality?"640 x 360 (higher load)":"480 x 272 (standard)");
        for(int i=0;i<(media_audio?1:4);i++)text_at(lines[i],35,105+i*31,492,i==option_row?selected:normal);
    }else{
        int start=(entry_index/8)*8;
        for(int i=start;i<entry_count&&i<start+8;i++){char line[290];snprintf(line,sizeof(line),"%s %s",entries[i].folder?">":entries[i].audio?"~":"*",entries[i].name);text_at(line,35,64+(i-start)*28,492,i==entry_index?selected:normal);}
        if(!entry_count)text_at("No entries",35,90,492,normal);
    }
    if(first_frame)startup_note("left text rendered");
    clip(NULL);
    if(cover&&(options||playing||panel==3)){SDL_Rect r={576,142,80,112};SDL_RenderCopy(renderer,cover,NULL,&r);}
    side(panel==1?"HELP":panel==2?"SETTINGS":panel==3?"INFO":playing?(paused?"PAUSED":"PLAYING"):options?"OPTIONS":"LIBRARY",0);
    if(fetch.thread){side("Loading...",2);side("B: Cancel",4);}
    else{side("A: Select",1);if(!cover||(!options&&!playing&&panel!=3)){side("B: Back",3);side("X: Settings",4);side("Y: Help",5);}side("Back: Exit",8);}
    SDL_Rect footer={27,303,666,38};clip(&footer);text_at(status,32,308,650,normal);clip(NULL);
    receiver();
}
static void menu_draw(void){menu_render();if(playing&&media_audio&&fullscreen&&!panel){color(0,0,0);SDL_RenderClear(renderer);spectrum_draw(1);text_at(media_name,30,15,660,(SDL_Color){235,235,235,255});}if(first_frame)startup_note("before first present");SDL_RenderPresent(renderer);if(first_frame){startup_note("first present complete");first_frame=0;}}
static void frame_draw(plm_frame_t *f){
    if(!video_texture||texture_w!=(int)f->width||texture_h!=(int)f->height){if(video_texture)SDL_DestroyTexture(video_texture);texture_w=f->width;texture_h=f->height;video_texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_IYUV,SDL_TEXTUREACCESS_STREAMING,texture_w,texture_h);}
    if(!video_texture){stream_error(SDL_GetError());return;}
    SDL_UpdateYUVTexture(video_texture,NULL,f->y.data,f->y.width,f->cb.data,f->cb.width,f->cr.data,f->cr.width);
    if(fullscreen){SDL_RenderSetScale(renderer,1,1);color(0,0,0);SDL_RenderClear(renderer);
        double ratio=(double)f->width/f->height;int w=width,h=(int)(w/ratio);if(h>height){h=height;w=(int)(h*ratio);}SDL_Rect dst={(width-w)/2,(height-h)/2,w,h};SDL_RenderCopy(renderer,video_texture,NULL,&dst);SDL_RenderPresent(renderer);
    }else{menu_render();SDL_RenderSetScale(renderer,(float)width/720,(float)height/480);color(0,0,0);SDL_Rect panel={27,60,506,232};SDL_RenderFillRect(renderer,&panel);double ratio=(double)f->width/f->height;int w=506,h=(int)(w/ratio);if(h>232){h=232;w=(int)(h*ratio);}SDL_Rect dst={27+(506-w)/2,60+(232-h)/2,w,h};SDL_RenderCopy(renderer,video_texture,NULL,&dst);SDL_RenderPresent(renderer);}
}
static void button_down(int button){
    /* Back must also work while a catalog request or playback is active. */
    if(button==SDL_CONTROLLER_BUTTON_BACK){quit=1;return;}
    if(retry_pending){if(button==SDL_CONTROLLER_BUTTON_B){retry_pending=0;snprintf(status,sizeof(status),"Retry cancelled; select Play to resume");}return;}
    if(fetch.thread&&fetch.type!=3){if(button==SDL_CONTROLLER_BUTTON_B){fetch_stop();autoplay_pending=0;snprintf(status,sizeof(status),"Cancelled");}return;}
    if(panel){
        if(button==SDL_CONTROLLER_BUTTON_B||button==SDL_CONTROLLER_BUTTON_Y){if(panel==2)preferences(1);panel=0;return;}
        if(panel==2){
            if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)panel_row=(panel_row+7)%8;
            if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)panel_row=(panel_row+1)%8;
            int d=button==SDL_CONTROLLER_BUTTON_DPAD_LEFT?-1:1;
            if(button==SDL_CONTROLLER_BUTTON_A||button==SDL_CONTROLLER_BUTTON_DPAD_LEFT||button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT){
                switch(panel_row){case 0:auto_next=!auto_next;break;case 1:repeat_one=!repeat_one;break;case 2:shuffle_music=!shuffle_music;break;
                case 3:show_spectrum=!show_spectrum;break;case 4:quality=!quality;break;
                case 5:volume+=d*5;if(volume<0)volume=0;if(volume>100)volume=100;break;
                case 6:prefer_audio=(prefer_audio+7+d)%7;break;case 7:prefer_subtitle=(prefer_subtitle+7+d)%7;break;}
            }
        }return;
    }
    if(playing){
        if(button==SDL_CONTROLLER_BUTTON_B){report_playback("stopped");report_started=0;start_position=player_position();player_stop();preferences(1);snprintf(status,sizeof(status),"Stopped");}
        else if(button==SDL_CONTROLLER_BUTTON_A||button==SDL_CONTROLLER_BUTTON_START){if(!paused)report_playback("paused");player_pause();report_time=0;}
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
            else if(option_row==3)quality=!quality;
        }return;
    }
    if(button==SDL_CONTROLLER_BUTTON_B&&has_parent)browse(parent_path,0);
    else if(button==SDL_CONTROLLER_BUTTON_X){panel=2;panel_row=0;}
    else if(button==SDL_CONTROLLER_BUTTON_Y)panel=1;
    else if(button==SDL_CONTROLLER_BUTTON_START)browse(folder_path,page_offset);
    else if(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT&&page_offset>=64)browse(folder_path,page_offset-64);
    else if(button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT&&page_offset+64<total_entries)browse(folder_path,page_offset+64);
    else if(button==SDL_CONTROLLER_BUTTON_DPAD_UP&&entry_count)entry_index=(entry_index+entry_count-1)%entry_count;
    else if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN&&entry_count)entry_index=(entry_index+1)%entry_count;
    else if(button==SDL_CONTROLLER_BUTTON_A&&entry_count){Entry *e=&entries[entry_index];if(e->folder)browse(e->target,0);else select_media(e);}
}
int main(void){
    FILE *boot=fopen("D:\\xbox-player.log","w");if(boot){fputs("Xbox player 0.3.0 daily playback\n",boot);fclose(boot);}
    preferences(0);snprintf(report_client,sizeof(report_client),"xbox-%08lx-%08lx",(unsigned long)GetTickCount(),(unsigned long)KeQueryPerformanceCounter());
    startup_note("entry: before graphics/input initialization");
    XVideoSetMode(640,480,32,REFRESH_DEFAULT);int configured=config();
    if(output_height!=480){void *p=NULL;VIDEO_MODE mode;while(XVideoListModes(&mode,32,0,&p)){if(mode.height==output_height&&mode.width==(output_height==720?1280:output_height==1080?1920:720)){XVideoSetMode(mode.width,mode.height,32,mode.refresh);break;}}}
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
    menu_draw();startup_note("network initialization");int network=nxNetInit(NULL);startup_note("network initialization returned");if(configured&&!network)browse("",0);else snprintf(status,sizeof(status),"Check server.cfg / Ethernet (network=%d)",network);
    startup_note("main: entering event loop");
    SDL_Event event;Uint32 draw=0,repeat=0;int held=-1,first_poll=1,first_loop=1,trigger_held[2]={0,0};
    while(!quit){
        if(first_poll)startup_note("main: before first event poll");
        while(SDL_PollEvent(&event)){
            if(event.type==SDL_CONTROLLERDEVICEADDED){controller_open(event.cdevice.which);}
            else if(event.type==SDL_CONTROLLERDEVICEREMOVED){held=-1;for(int i=0;i<4;i++)if(pads[i]&&SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i]))==event.cdevice.which){SDL_GameControllerClose(pads[i]);pads[i]=NULL;}}
            else if(event.type==SDL_CONTROLLERBUTTONDOWN){button_down(event.cbutton.button);if(event.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_UP||event.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_DOWN){held=event.cbutton.button;repeat=SDL_GetTicks()+400;}}
            else if(event.type==SDL_CONTROLLERBUTTONUP&&held==event.cbutton.button)held=-1;
            else if(event.type==SDL_CONTROLLERAXISMOTION&&(event.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERLEFT||event.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERRIGHT)){
                int dir=event.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERRIGHT,down=event.caxis.value>16000;
                if(down&&!trigger_held[dir]&&playing&&!media_live){double pos=player_position(),target=-1;
                    if(dir){for(int i=0;i<chapter_count;i++)if(chapter_starts[i]>pos+1){target=chapter_starts[i];break;}}
                    else for(int i=chapter_count-1;i>=0;i--)if(chapter_starts[i]<pos-2){target=chapter_starts[i];break;}
                    if(target>=0){player_start(target);report_time=0;}
                }trigger_held[dir]=down;
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
            if(type==3){fetch.body=body;if(!*fetch.error)artwork_read();fetch.body=NULL;snprintf(status,sizeof(status),"A: Play | X: Resume/start | Y: Info");}
            else if(*fetch.error){autoplay_pending=0;snprintf(status,sizeof(status),"%s",fetch.error);}
            else if(type==2){
                if(parse_json(body)){text_tok(field(0,"id"),following.target,sizeof(following.target));text_tok(field(0,"name"),following.name,sizeof(following.name));char kind[16];text_tok(field(0,"kind"),kind,sizeof(kind));following.audio=!strcmp(kind,"audio");}
                if(!*following.target)snprintf(status,sizeof(status),"End of folder (no next/previous file)");
            }else if(!(type?parse_metadata(body):parse_catalog(body))){autoplay_pending=0;snprintf(status,sizeof(status),"Invalid reply. Update server to 0.1.70.");}
            else{options=type;option_row=0;snprintf(status,sizeof(status),options?"A: Play | X: Resume/start | Y: Info":"%d entries | Left/Right: page | A: Open",total_entries);
                if(type==1){apply_metadata_preferences();want_art=!strncmp(media_id,"plex.",5)||!strncmp(media_id,"jellyfin.",9)||!strncmp(media_id,"dlna.",5);
                    if(autoplay_pending){autoplay_pending=0;begin_playback(0);want_art=0;}}
            }
            free(body);
            if(*following.target){autoplay_pending=1;select_media(&following);}
            else if(want_art){char encoded[4700],request[6000];url_encode(media_id,encoded,sizeof(encoded));snprintf(request,sizeof(request),"/api/psp-artwork?item=%s",encoded);fetch_start(request,3);}
            startup_note(status);
        }
        if(first_loop)startup_note("main: worker done check returned");
        if(retry_pending&&(Sint32)(SDL_GetTicks()-retry_at)>=0){retry_pending=0;if(!player_start(retry_position))snprintf(status,sizeof(status),"Retry failed; select Play to resume");}
        if(playing){int result=player_tick();
            if(!paused&&(audio_running||rendered)&&(!report_started||SDL_GetTicks()-report_time>=15000)){report_started=1;report_playback("playing");}
            if(result==1){frame_draw(next_frame);next_frame=NULL;}
            else if(result<0||result==2){
                report_playback("stopped");report_started=0;start_position=player_position();
                int retry=result<0&&(strstr(stream.error,"network")||strstr(stream.error,"interrupted")||strstr(stream.error,"Truncated"));
                snprintf(status,sizeof(status),"%s",result==2?"Playback finished":*stream.error?stream.error:"Stream failed");player_stop();
                if(result==2&&!media_live){start_position=0;if(repeat_one)begin_playback(0);else if(auto_next)next_media(0);}
                else if(retry&&retry_count<3){retry_count++;retry_pending=1;retry_position=media_live?0:start_position;retry_at=SDL_GetTicks()+2000;snprintf(status,sizeof(status),"Connection lost: retry %d/3 | B: Cancel",retry_count);}
            }
        }
        report_tick();
        if(first_loop)startup_note("main: before clock/redraw");
        if((!playing||media_audio||paused)&&SDL_GetTicks()-draw>=66){menu_draw();draw=SDL_GetTicks();}
        if(first_loop)startup_note("main: before yield");
        SDL_Delay(1);
        if(first_loop){startup_note("main: first loop complete");first_loop=0;}
    }
    report_playback("stopped");fetch_stop();player_stop();report_shutdown();preferences(1);if(cover)SDL_DestroyTexture(cover);if(backdrop)SDL_DestroyTexture(backdrop);if(audio_initialized)XAudioPause();if(pcm)MmFreeContiguousMemory(pcm);
    for(int i=0;i<4;i++)if(pads[i])SDL_GameControllerClose(pads[i]);nxNetShutdown();TTF_CloseFont(font);SDL_DestroyTexture(skin);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);TTF_Quit();SDL_Quit();XReboot();return 0;
}

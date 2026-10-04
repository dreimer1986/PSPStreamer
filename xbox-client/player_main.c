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
#include "render_clip.h"

static void menu_draw(void);

static SDL_Window *window;static SDL_Renderer *renderer;static TTF_Font *font;static SDL_Texture *skin;
static SDL_GameController *pads[4];
static int width=640,height=480,options,fullscreen=1,quit;
static char status[160]="Connecting...";static float meters[2];
static int first_frame=1;
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
typedef struct {SDL_Thread *thread;SDL_atomic_t cancel,done;char path[6000],error[100];char *body;int type;} Fetch;
static Fetch fetch;
static int fetch_worker(void *unused){
    (void)unused;Http h;
    startup_note("catalog: worker entered, opening HTTP");
    if(!http_open(&h,fetch.path,&fetch.cancel)){snprintf(fetch.error,sizeof(fetch.error),"HTTP %d / connection failed",h.status);goto done;}
    startup_note("catalog: HTTP headers received");
    fetch.body=malloc(512*1024);if(!fetch.body){snprintf(fetch.error,sizeof(fetch.error),"Out of JSON memory");http_close(&h);goto done;}
    unsigned at=0;int n=0;
    while(at<512*1024-1&&(n=http_read(&h,fetch.body+at,512*1024-1-at))>0)at+=n;
    fetch.body[at]=0;http_close(&h);
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
static void select_media(Entry *e){snprintf(media_id,sizeof(media_id),"%s",e->target);snprintf(media_name,sizeof(media_name),"%s",e->name);media_audio=e->audio;char encoded[4700],request[6000];url_encode(media_id,encoded,sizeof(encoded));snprintf(request,sizeof(request),"/api/metadata/%s",encoded);fetch_start(request,1);}
static void color(int r,int g,int b){SDL_SetRenderDrawColor(renderer,r,g,b,255);}
static void text_at(const char *s,int x,int y,int max_width,SDL_Color color){
    if(!*s)return;SDL_Surface *surface=TTF_RenderUTF8_Blended(font,s,color);if(!surface)return;
    SDL_Texture *texture=SDL_CreateTextureFromSurface(renderer,surface);
    SDL_Rect src={0,0,surface->w>max_width?max_width:surface->w,surface->h},dst={x,y,src.w,src.h};
    if(texture){SDL_RenderCopy(renderer,texture,&src,&dst);SDL_DestroyTexture(texture);}SDL_FreeSurface(surface);
}
static void side(const char *s,int row){SDL_Color c={220,226,227,255};text_at(s,565,68+row*24,120,c);}
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
    if(playing){
        text_at(media_name,35,70,492,selected);char line[120];double p=player_position();
        snprintf(line,sizeof(line),"%s  %02d:%02d / %02d:%02d",paused?"Paused":"Playing",(int)p/60,(int)p%60,(int)media_duration/60,(int)media_duration%60);text_at(line,35,105,492,normal);
        text_at("A: Pause / Resume   B: Stop",35,160,492,normal);
        text_at("Left / Right: -/+30s   Up / Down: Volume",35,190,492,normal);
    }else if(options){
        text_at(media_name,35,66,492,selected);
        char lines[4][180];snprintf(lines[0],180,"Play");snprintf(lines[1],180,"Audio: %s",audio_count?audio_labels[audio_track]:"No audio");
        snprintf(lines[2],180,"Subtitles: %s",subtitle_track<0?"Off":subtitle_labels[subtitle_track]);
        snprintf(lines[3],180,"Video: %s",quality?"640 x 360 (higher load)":"480 x 272 (standard)");
        for(int i=0;i<(media_audio?2:4);i++)text_at(lines[i],35,105+i*31,492,i==option_row?selected:normal);
    }else{
        int start=(entry_index/8)*8;
        for(int i=start;i<entry_count&&i<start+8;i++){char line[290];snprintf(line,sizeof(line),"%s %s",entries[i].folder?">":entries[i].audio?"~":"*",entries[i].name);text_at(line,35,64+(i-start)*28,492,i==entry_index?selected:normal);}
        if(!entry_count)text_at("No entries",35,90,492,normal);
    }
    if(first_frame)startup_note("left text rendered");
    clip(NULL);
    side(playing?(paused?"PAUSED":"PLAYING"):options?"OPTIONS":"LIBRARY",0);
    if(fetch.thread){side("Loading...",2);side("B: Cancel",4);}
    else{side("A: Select",2);side("B: Back",3);side("X: Refresh",4);side("Y: Fullscreen",5);side("Back: Exit",7);}
    SDL_Rect footer={27,303,666,38};clip(&footer);text_at(status,32,308,650,normal);clip(NULL);
    receiver();
}
static void menu_draw(void){menu_render();if(first_frame)startup_note("before first present");SDL_RenderPresent(renderer);if(first_frame){startup_note("first present complete");first_frame=0;}}
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
    if(fetch.thread){if(button==SDL_CONTROLLER_BUTTON_B){fetch_stop();snprintf(status,sizeof(status),"Cancelled");}return;}
    if(playing){
        if(button==SDL_CONTROLLER_BUTTON_B){player_stop();snprintf(status,sizeof(status),"Stopped");}
        else if(button==SDL_CONTROLLER_BUTTON_A||button==SDL_CONTROLLER_BUTTON_START)player_pause();
        else if(button==SDL_CONTROLLER_BUTTON_Y){fullscreen=!fullscreen;if(paused&&next_frame)frame_draw(next_frame);}
        else if(button==SDL_CONTROLLER_BUTTON_DPAD_UP){volume+=5;if(volume>100)volume=100;}
        else if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN){volume-=5;if(volume<0)volume=0;}
        else if(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT||button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT){double p=player_position()+(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT?-30:30);if(p<0)p=0;if(media_duration>1&&p>=media_duration)p=media_duration-1;player_start(p);}
        return;
    }
    if(options){
        int rows=media_audio?2:4;
        if(button==SDL_CONTROLLER_BUTTON_B){options=0;return;}
        if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)option_row=(option_row+rows-1)%rows;
        if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)option_row=(option_row+1)%rows;
        int d=button==SDL_CONTROLLER_BUTTON_DPAD_LEFT?-1:1;
        if(button==SDL_CONTROLLER_BUTTON_A||button==SDL_CONTROLLER_BUTTON_DPAD_LEFT||button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT){
            if(!option_row&&button==SDL_CONTROLLER_BUTTON_A){fullscreen=!media_audio;if(!player_start(0))snprintf(status,sizeof(status),"Cannot start player");else snprintf(status,sizeof(status),"Buffering... B: Stop");}
            else if(option_row==1&&audio_count)audio_track=(audio_track+audio_count+d)%audio_count;
            else if(option_row==2){subtitle_track=(subtitle_track+1+subtitle_count+1+d)%(subtitle_count+1)-1;}
            else if(option_row==3)quality=!quality;
        }return;
    }
    if(button==SDL_CONTROLLER_BUTTON_B&&has_parent)browse(parent_path,0);
    else if(button==SDL_CONTROLLER_BUTTON_X)browse(folder_path,page_offset);
    else if(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT&&page_offset>=64)browse(folder_path,page_offset-64);
    else if(button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT&&page_offset+64<total_entries)browse(folder_path,page_offset+64);
    else if(button==SDL_CONTROLLER_BUTTON_DPAD_UP&&entry_count)entry_index=(entry_index+entry_count-1)%entry_count;
    else if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN&&entry_count)entry_index=(entry_index+1)%entry_count;
    else if(button==SDL_CONTROLLER_BUTTON_A&&entry_count){Entry *e=&entries[entry_index];if(e->folder)browse(e->target,0);else select_media(e);}
}
int main(void){
    FILE *boot=fopen("D:\\xbox-player.log","w");if(boot){fputs("Xbox player 0.2.4 serial diagnostics\n",boot);fclose(boot);}
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
    SDL_Event event;Uint32 draw=0,repeat=0;int held=-1,first_poll=1,first_loop=1;
    while(!quit){
        if(first_poll)startup_note("main: before first event poll");
        while(SDL_PollEvent(&event)){
            if(event.type==SDL_CONTROLLERDEVICEADDED){controller_open(event.cdevice.which);}
            else if(event.type==SDL_CONTROLLERDEVICEREMOVED){held=-1;for(int i=0;i<4;i++)if(pads[i]&&SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i]))==event.cdevice.which){SDL_GameControllerClose(pads[i]);pads[i]=NULL;}}
            else if(event.type==SDL_CONTROLLERBUTTONDOWN){button_down(event.cbutton.button);if(event.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_UP||event.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_DOWN){held=event.cbutton.button;repeat=SDL_GetTicks()+400;}}
            else if(event.type==SDL_CONTROLLERBUTTONUP&&held==event.cbutton.button)held=-1;
        }
        if(first_poll){startup_note("main: first event poll complete");first_poll=0;}
        if(held>=0&&(Sint32)(SDL_GetTicks()-repeat)>=0){button_down(held);repeat=SDL_GetTicks()+100;}
        if(first_loop)startup_note("main: before worker done check");
        if(fetch.thread&&SDL_AtomicGet(&fetch.done)){
            startup_note("catalog: joining completed worker");
            SDL_WaitThread(fetch.thread,NULL);fetch.thread=NULL;
            startup_note("catalog: parsing response");
            if(*fetch.error)snprintf(status,sizeof(status),"%s",fetch.error);
            else if(!(fetch.type?parse_metadata(fetch.body):parse_catalog(fetch.body)))snprintf(status,sizeof(status),"Invalid reply. Update server to 0.1.68.");
            else{options=fetch.type;option_row=0;snprintf(status,sizeof(status),options?"Choose tracks with Left/Right, then Play":"%d entries | Left/Right: page | A: Open",total_entries);}
            free(fetch.body);fetch.body=NULL;
            startup_note(status);
        }
        if(first_loop)startup_note("main: worker done check returned");
        if(playing){int result=player_tick();if(result==1){frame_draw(next_frame);next_frame=NULL;}else if(result<0||result==2){snprintf(status,sizeof(status),"%s",result==2?"Playback finished":*stream.error?stream.error:"Stream failed");player_stop();}}
        if(first_loop)startup_note("main: before clock/redraw");
        if((!playing||media_audio||paused)&&SDL_GetTicks()-draw>=66){menu_draw();draw=SDL_GetTicks();}
        if(first_loop)startup_note("main: before yield");
        SDL_Delay(1);
        if(first_loop){startup_note("main: first loop complete");first_loop=0;}
    }
    fetch_stop();player_stop();if(audio_initialized)XAudioPause();if(pcm)MmFreeContiguousMemory(pcm);
    for(int i=0;i<4;i++)if(pads[i])SDL_GameControllerClose(pads[i]);nxNetShutdown();TTF_CloseFont(font);SDL_DestroyTexture(skin);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);TTF_Quit();SDL_Quit();XReboot();return 0;
}

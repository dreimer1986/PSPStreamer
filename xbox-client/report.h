/* SPDX-License-Identifier: GPL-2.0-or-later. Bounded asynchronous telemetry.
 * Main thread owns the queue; worker receives an immutable request. */
typedef struct {char body[4200];} PlaybackReport;
static PlaybackReport reports[8],active_report;
static unsigned report_read,report_count;
static unsigned report_sequence;
static SDL_Thread *report_thread;static SDL_atomic_t report_cancel,report_done;
static char report_client[48];static Uint32 report_time;static int report_started;
static char active_diagnostic[sizeof(player_diagnostic)];
static int report_worker(void *unused){
    (void)unused;
    if(*active_diagnostic){FILE *f=fopen("D:\\xbox-player.log","a");if(f){fputs(active_diagnostic,f);fclose(f);}}
    Http h;http_request(&h,"/api/client-playback",&report_cancel,active_report.body,5000);http_close(&h);SDL_AtomicSet(&report_done,1);return 0;
}
static void report_tick(void){
    if(report_thread&&SDL_AtomicGet(&report_done)){SDL_WaitThread(report_thread,NULL);report_thread=NULL;}
    if(!report_thread&&report_count){
        active_report=reports[report_read];report_read=(report_read+1)%8;report_count--;
        snprintf(active_diagnostic,sizeof(active_diagnostic),"%s",player_diagnostic);*player_diagnostic=0;
        SDL_AtomicSet(&report_done,0);report_thread=SDL_CreateThreadWithStackSize(report_worker,"telemetry",65536,NULL);
    }
}
static void report_playback(const char *state){
    if(!report_started||!*media_id)return;
    if(report_count==8){report_read=(report_read+1)%8;report_count--;}
    char hex[3073];static const char digits[]="0123456789abcdef";unsigned i;
    for(i=0;media_id[i]&&i<1536;i++){unsigned c=(unsigned char)media_id[i];hex[i*2]=digits[c>>4];hex[i*2+1]=digits[c&15];}hex[i*2]=0;
    char name_hex[513];for(i=0;media_name[i]&&i<256;i++){unsigned c=(unsigned char)media_name[i];name_hex[i*2]=digits[c>>4];name_hex[i*2+1]=digits[c&15];}name_hex[i*2]=0;
    double pos=player_position();if(pos<0)pos=0;if(pos>604800)pos=604800;
    double dur=media_duration;if(dur>604800)dur=604800;
    snprintf(reports[(report_read+report_count)%8].body,sizeof(reports[0].body),
        "{\"client\":\"%s\",\"sequence\":%u,\"id_hex\":\"%s\",\"name_hex\":\"%s\",\"state\":\"%s\",\"kind\":\"%s\",\"position\":%u,\"duration\":%u}",
        report_client,++report_sequence,hex,name_hex,state,media_audio?"audio":"video",(unsigned)(pos*1000),(unsigned)(dur*1000));report_count++;report_time=SDL_GetTicks();
}
static void report_shutdown(void){
    Uint32 start=SDL_GetTicks();while((report_count||report_thread)&&SDL_GetTicks()-start<2000){report_tick();SDL_Delay(10);}
    SDL_AtomicSet(&report_cancel,1);if(report_thread)SDL_WaitThread(report_thread,NULL);report_thread=NULL;
}

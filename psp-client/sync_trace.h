/* Bounded RAM-only measurements during video playback. Written after all
 * playback threads stop, so Memory Stick latency cannot perturb A/V timing.
 * DAC PTS is the submitted block reference, NOT a measurement at the speaker.
 */
#define SYNC_TRACE_CAPACITY 4096
typedef struct {
    unsigned int elapsed_ms;
    int video_pts, audio_pts, rest_samples, queued_blocks;
    unsigned int decode_us, prepare_us, copy_us;
    int action, shown, dropped, pts_errors;
} SyncTraceRow;
static SyncTraceRow sync_trace[SYNC_TRACE_CAPACITY];
static unsigned int sync_trace_count, sync_trace_shown, sync_trace_dropped;
static unsigned long long sync_trace_start, sync_trace_last;
static unsigned int sync_decode_us, sync_prepare_us;
static volatile int sync_audio_channel = -1, sync_audio_pts_errors;
static void sync_trace_reset(void) {
    if(!debug_enabled) return;
    sync_trace_count = sync_trace_shown = sync_trace_dropped = 0;
    sync_audio_pts_errors = 0;
    sync_trace_start = sceKernelGetSystemTimeWide(); sync_trace_last = 0;
}
static void sync_trace_record(int video_pts, int action, unsigned int copy_us) {
    if(!debug_enabled) return;
    unsigned long long now = sceKernelGetSystemTimeWide();
    unsigned int audio_pts = audio_current_timestamp_ms;
    SyncTraceRow *row;
    int rest = sync_audio_channel >= 0 ? sceAudioGetChannelRestLen(sync_audio_channel) : -1;
    if (audio_pts != audio_current_timestamp_ms) rest = -1; /* sample crossed a submission */
    if (action == 1) sync_trace_shown++;
    if (action == 2) sync_trace_dropped++;
    if (sync_trace_count >= 32 && now - sync_trace_last < 1000000ULL) return;
    sync_trace_last = now;
    row = &sync_trace[sync_trace_count++ % SYNC_TRACE_CAPACITY];
    row->elapsed_ms = (unsigned int)((now - sync_trace_start) / 1000ULL);
    row->video_pts = video_pts; row->audio_pts = (int)audio_pts;
    row->rest_samples = rest;
    row->queued_blocks = audio_blocks_published - audio_played_blocks;
    row->decode_us = sync_decode_us; row->prepare_us = sync_prepare_us;
    row->copy_us = copy_us; row->action = action;
    row->shown = sync_trace_shown; row->dropped = sync_trace_dropped;
    row->pts_errors = sync_audio_pts_errors;
}
static int sync_trace_write_all(SceUID fd,const char *data,int size) {
    while(size>0) {
        int written=sceIoWrite(fd,data,size);
        if(written<=0 || written>size)return 0;
        data+=written;size-=written;
    }
    return 1;
}
static void sync_trace_save(int tv, int result, int start_seconds) {
    if(!debug_enabled) return;
    char line[320];
    /* Playback has stopped. Batch tiny CSV records instead of thousands of
     * Memory Stick calls; no background writer or extra playback lifetime. */
    static char buffer[4096];
    int used=0,ok=1;
    unsigned int i, first = sync_trace_count > SYNC_TRACE_CAPACITY ? sync_trace_count - SYNC_TRACE_CAPACITY : 0;
    if(diagnostic_rotate(tv?"ms0:/PSP/SYSTEM/PSPStreamer-sync-tv.csv":
        "ms0:/PSP/SYSTEM/PSPStreamer-sync-lcd.csv")<0)return;
    SceUID fd = sceIoOpen(tv ? "ms0:/PSP/SYSTEM/PSPStreamer-sync-tv.csv" :
                              "ms0:/PSP/SYSTEM/PSPStreamer-sync-lcd.csv",
                         PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    int size;
    if (fd < 0) return;
    size = snprintf(line, sizeof(line), "# result=%d,start_seconds=%d,shown=%u,dropped=%u,pts_errors=%d\n"
        "elapsed_ms,video_pts_ms,audio_block_pts_ms,dac_rest_samples,queued_blocks,decode_us,prepare_us,copy_us,action,shown,dropped,pts_errors\n",
        result, start_seconds, sync_trace_shown, sync_trace_dropped, sync_audio_pts_errors);
    if(size<0 || size>=(int)sizeof(line)) {sceIoClose(fd);return;}
    memcpy(buffer,line,size);used=size;
    for (i = first; i < sync_trace_count; i++) {
        SyncTraceRow *r = &sync_trace[i % SYNC_TRACE_CAPACITY];
        size = snprintf(line, sizeof(line), "%u,%d,%d,%d,%d,%u,%u,%u,%d,%d,%d,%d\n",
            r->elapsed_ms, r->video_pts, r->audio_pts, r->rest_samples, r->queued_blocks,
            r->decode_us, r->prepare_us, r->copy_us, r->action, r->shown, r->dropped, r->pts_errors);
        if(size<0 || size>=(int)sizeof(line)) {ok=0;break;}
        if(used+size>(int)sizeof(buffer)) {
            if(!sync_trace_write_all(fd,buffer,used)){ok=0;break;}
            used=0;
        }
        memcpy(buffer+used,line,size);used+=size;
    }
    if(ok && used)sync_trace_write_all(fd,buffer,used);
    sceIoClose(fd);
}

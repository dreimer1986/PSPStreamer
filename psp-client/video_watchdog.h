/* Diagnostic only: no cancellation, codec calls or scheduling adjustments.
 * Write only after a suspected stall, never in healthy playback. */
static volatile int video_watch_running;
static int video_watch_id=-1;
static const char * volatile video_watch_stage="idle";
static volatile unsigned int video_watch_tick;
static unsigned int video_watch_now(void) {
    return (unsigned int)(sceKernelGetSystemTimeWide()/1000ULL);
}
static void video_watch_ping(const char *stage) {
    video_watch_stage=stage;
    video_watch_tick=video_watch_now();
}
static int video_watch_worker(SceSize args,void *argp) {
    unsigned int last_progress=video_watch_now(),last_report=0;
    int last_position=playback_position_ms,reports=0;
    (void)args; (void)argp;
    while(video_watch_running) {
        unsigned int now=video_watch_now();
        int position=playback_position_ms;
        if(position!=last_position || playback_paused || !timed_playing) {
            last_position=position; last_progress=now;
        }
        if(reports<4 && (now-video_watch_tick>=8000 || now-last_progress>=8000) &&
           (!reports || now-last_report>=30000)) {
            char text[1024];
            int n=snprintf(text,sizeof(text),
                "tick_ms=%u tv=%d stage=%s hw=%s heartbeat_age_ms=%u progress_age_ms=%u\n"
                "position_ms=%d paused=%d timed_running=%d eof=%d error=%d audio_done=%d\n"
                "video_queue=%u audio_queue=%u audio_running=%d audio_state=%d published=%u played=%u\n"
                "threads: reader=%d dac=%d remote=%d\n\n",
                now,tvout_video_active,video_watch_stage,h264_hw_last_step(),
                now-video_watch_tick,now-last_progress,position,playback_paused,
                timed_running,timed_eof,timed_error,timed_audio_done,
                timed_video.write-timed_video.read,timed_audio.write-timed_audio.read,
                audio_running,audio_state,(unsigned int)audio_blocks_published,
                (unsigned int)audio_played_blocks,timed_reader_id,
                (int)audio_output_thread_id,remote_control_thread_id);
            SceUID fd=sceIoOpen("video-stall.txt",PSP_O_WRONLY|PSP_O_CREAT|
                (reports?PSP_O_APPEND:PSP_O_TRUNC),0777);
            if(fd>=0) { sceIoWrite(fd,text,n<(int)sizeof(text)?n:(int)sizeof(text)-1); sceIoClose(fd); }
            reports++; last_report=now;
        }
        sceKernelDelayThread(100000);
    }
    return 0;
}
static void video_watch_start(void) {
    video_watch_ping("video startup");
    video_watch_running=1;
    video_watch_id=sceKernelCreateThread("PSPVideoWatch",video_watch_worker,0x3F,0x2000,0,NULL);
    if(video_watch_id>=0 && sceKernelStartThread(video_watch_id,0,NULL)<0) {
        sceKernelDeleteThread(video_watch_id); video_watch_id=-1;
    }
    if(video_watch_id<0) video_watch_running=0;
}
static void video_watch_stop(void) {
    video_watch_running=0;
    if(video_watch_id>=0) {
        sceKernelWaitThreadEnd(video_watch_id,NULL);
        sceKernelDeleteThread(video_watch_id); video_watch_id=-1;
    }
}

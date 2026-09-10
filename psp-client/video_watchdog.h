/* Diagnostic only: no cancellation, codec calls or scheduling adjustments.
 * Probe storage before playback; write during playback only on a stall. */
static int video_watch_music;
static volatile int video_watch_ready;
static const char *video_watch_path;
static int video_watch_write(const char *text, int truncate) {
    int size=(int)strlen(text), result;
    SceUID fd=sceIoOpen(video_watch_path,PSP_O_WRONLY|PSP_O_CREAT|
        (truncate?PSP_O_TRUNC:PSP_O_APPEND),0777);
    if(fd<0) return fd;
    result=sceIoWrite(fd,text,size);
    int closed=sceIoClose(fd);
    return result<0?result:result!=size?-5:closed;
}
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
    unsigned int last_remote=last_progress, remote_completed=remote_http_completed;
    int last_position=video_watch_music?(int)audio_played_blocks:playback_position_ms,reports=0;
    (void)args; (void)argp;
    video_watch_ready=video_watch_write("monitor running\n",0)<0?-1:1;
    while(video_watch_running) {
        unsigned int now=video_watch_now();
        if(remote_completed!=remote_http_completed) {
            remote_completed=remote_http_completed;
            if(remote_http_last_result>=0) last_remote=now;
        }
        int position=video_watch_music?(int)audio_played_blocks:playback_position_ms;
        if(position!=last_position || (video_watch_music?!audio_start:(playback_paused || !timed_playing))) {
            last_position=position; last_progress=now;
        }
        if(reports<4 && (now-video_watch_tick>=8000 || now-last_progress>=8000 || now-last_remote>=8000) &&
           (!reports || now-last_report>=30000)) {
            char text[1024];
            int n=snprintf(text,sizeof(text),
                "tick_ms=%u tv=%d stage=%s hw=%s heartbeat_age_ms=%u progress_age_ms=%u\n"
                "position_ms=%d paused=%d timed_running=%d eof=%d error=%d audio_done=%d\n"
                "video_queue=%u audio_queue=%u audio_running=%d audio_state=%d published=%u played=%u\n"
                "threads: reader=%d dac=%d remote=%d\n"
                "remote: phase=%s attempts=%u completed=%u result=%d sequence=%d success_age_ms=%u\n\n",
                now,tvout_video_active,video_watch_stage,h264_hw_last_step(),
                now-video_watch_tick,now-last_progress,position,playback_paused,
                timed_running,timed_eof,timed_error,timed_audio_done,
                timed_video.write-timed_video.read,timed_audio.write-timed_audio.read,
                audio_running,audio_state,(unsigned int)audio_blocks_published,
                (unsigned int)audio_played_blocks,timed_reader_id,
                (int)audio_output_thread_id,remote_control_thread_id,
                remote_http_stage,remote_http_attempts,remote_http_completed,
                remote_http_last_result,remote_control_sequence,now-last_remote);
            (void)n;
            video_watch_write(text,0);
            reports++; last_report=now;
        }
        sceKernelDelayThread(100000);
    }
    return 0;
}
static int video_watch_start(int music) {
    int result, attempts;
    video_watch_music=music;
    video_watch_path=music?"ms0:/PSP/SYSTEM/PSPStreamer-watch-music.txt":"ms0:/PSP/SYSTEM/PSPStreamer-watch-video.txt";
    sceIoMkdir("ms0:/PSP/SYSTEM",0777);
    result=video_watch_write(music?"music startup\n":"video startup\n",1);
    if(result<0) return result;
    video_watch_ready=0;
    video_watch_ping("playback startup");
    video_watch_running=1;
    video_watch_id=sceKernelCreateThread("PSPVideoWatch",video_watch_worker,0x3F,0x2000,0,NULL);
    result=video_watch_id<0?video_watch_id:sceKernelStartThread(video_watch_id,0,NULL);
    if(video_watch_id>=0 && result<0) {
        sceKernelDeleteThread(video_watch_id); video_watch_id=-1;
    }
    if(video_watch_id<0) { video_watch_running=0; video_watch_write("monitor thread failed\n",0); return result; }
    for(attempts=0; attempts<100 && !video_watch_ready; attempts++) sceKernelDelayThread(10000);
    return video_watch_ready==1?0:-5;
}
static void video_watch_stop(void) {
    video_watch_running=0;
    if(video_watch_id>=0) {
        sceKernelWaitThreadEnd(video_watch_id,NULL);
        sceKernelDeleteThread(video_watch_id); video_watch_id=-1;
        video_watch_write("monitor stopped\n",0);
    }
}

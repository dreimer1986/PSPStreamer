/* Transport recovery and explicitly bounded AVC decoder recovery.
 * Called after playback has joined all workers and released its buffers. */
static int recovery_failures,recovery_reset_done;
static unsigned long long recovery_last_reset;
static void playback_recovery_reset(void) {
    recovery_failures=recovery_reset_done=0;recovery_last_reset=0;
}
static void playback_recovery_status(TextId text) {
    lcd_music_reset();tv_music_reset();
    if(tv_ui_active)tv_draw_music(tr(text),0);else lcd_draw_music(tr(text),0);
}
static int playback_recovery_associate(void) {
    unsigned long long now=sceKernelGetSystemTimeWide();
    int force=recovery_failures>=3 &&
        (!recovery_reset_done || now-recovery_last_reset>=60000000ULL);
    recovery_log(force?"WLAN reset":"connection retry",recovery_failures,0,"association");
    int state=0;
    sceNetApctlGetState(&state);
    playback_recovery_status(force || state!=PSP_NET_APCTL_STATE_GOT_IP?
        TXT_STREAM_WIFI:TXT_STREAM_SERVER);
    /* Playback and media-remote workers are already joined. Also stop the
     * independent virtual-button request before disconnecting the AP. */
    input_remote_stop();
    network_ready=http_ready=0;
    radio_connect_wait=1;
    int result;
    if(force) {
        recovery_last_reset=now;recovery_reset_done=1;recovery_failures=0;
        result=wifi_associate(1);
        network_ready=http_ready=result==0;
    } else result=wait_for_network_restore();
    radio_connect_wait=0;
    recovery_log("association finished",result,0,force?"forced":"normal");
    if(result==0)playback_recovery_status(TXT_STREAM_RESUME);
    return result;
}
static int playback_recover_wait(int network) {
    if(network)recovery_failures++;
    unsigned int old=~0U;
    int paused=0;
    unsigned long long retry=sceKernelGetSystemTimeWide()+5000000ULL;
    plex_paused=1;plex_started=0;
    playback_recovery_status(network?TXT_STREAM_RECONNECT:TXT_STREAM_DECODER);
    if(music_remote_start()<0)return 0;
    for(;;) {
        SceCtrlData pad;
        keep_awake();sceCtrlPeekBufferPositive(&pad,1);
        unsigned int pressed=pad.Buttons & ~old;
        int action=music_remote_action;
        if(comfort_expired() || action==MUSIC_REMOTE_STOP || action==MUSIC_REMOTE_PLAY ||
           (pressed & (PSP_CTRL_START|PSP_CTRL_CIRCLE))) {
            recovery_log("recovery cancelled",action,0,"waiting");music_remote_stop();return 0;
        }
        if(action==MUSIC_REMOTE_PAUSE)paused=1;
        if(action==MUSIC_REMOTE_SEEK) {
            playback_position_ms=music_remote_seconds*1000;
            paused=0;retry=0;
        }
        if(action==MUSIC_REMOTE_RESUME) {paused=0;retry=0;}
        music_remote_action=MUSIC_REMOTE_NONE;
        if((pressed & (PSP_CTRL_CROSS|PSP_CTRL_SQUARE)) ||
           (!paused && (unsigned long long)sceKernelGetSystemTimeWide()>=retry)) {
            music_remote_stop();
            if(!network)return 1; /* Decoder retry must not reset Wi-Fi. */
            int connected=playback_recovery_associate();
            if(connected==0)return 1;
            if(connected==-5)return 0;
            recovery_failures++;
            playback_recovery_status(TXT_STREAM_RECONNECT);
            retry=sceKernelGetSystemTimeWide()+5000000ULL;
            if(music_remote_start()<0)return 0;
        }
        old=pad.Buttons;sceKernelDelayThread(20000);
    }
}
static int playback_reconnect_wait(void) {return playback_recover_wait(1);}
static int playback_decoder_retry(int result,const char *stage,int progress_ms,int *failures) {
    if(strcmp(stage,"AVC: Decode") ||
       ((unsigned int)result!=0x80628001U && (unsigned int)result!=0x80628002U))return 0;
    if(progress_ms>=30000)*failures=0;
    if(*failures>=3)return -1;
    ++*failures;
    return 1;
}
static int playback_recovery_position(int position_ms) {
    return position_ms>0?position_ms/1000:0;
}
static void playback_recovery_cancel(void) {
    playback_reached_end=resume_pending=seek_requested=0;
    video_file_direction=0;
}

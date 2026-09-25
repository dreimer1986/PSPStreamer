/* Retry only transport failures, never local media, codec or format errors.
 * Called after playback has joined all workers and released its buffers. */
static int playback_reconnect_wait(void) {
    unsigned int old=~0U;
    int paused=0;
    unsigned long long retry=sceKernelGetSystemTimeWide()+5000000ULL;
    plex_paused=1;plex_started=0;
    lcd_music_reset();tv_music_reset();
    if(tv_ui_active)tv_draw_music(tr(TXT_STREAM_RECONNECT),0);
    else lcd_draw_music(tr(TXT_STREAM_RECONNECT),0);
    if(music_remote_start()<0)return 0;
    for(;;) {
        SceCtrlData pad;
        keep_awake();sceCtrlPeekBufferPositive(&pad,1);
        unsigned int pressed=pad.Buttons & ~old;
        int action=music_remote_action;
        if(comfort_expired() || action==MUSIC_REMOTE_STOP || action==MUSIC_REMOTE_PLAY ||
           (pressed & (PSP_CTRL_START|PSP_CTRL_CIRCLE))) {
            music_remote_stop();return 0;
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
            radio_connect_wait=1;
            int connected=wait_for_network_restore();
            radio_connect_wait=0;
            if(connected==0)return 1;
            if(connected==-5)return 0;
            retry=sceKernelGetSystemTimeWide()+5000000ULL;
            if(music_remote_start()<0)return 0;
        }
        old=pad.Buttons;sceKernelDelayThread(20000);
    }
}
static int playback_recovery_position(int position_ms) {
    return position_ms>0?position_ms/1000:0;
}
static void playback_recovery_cancel(void) {
    playback_reached_end=resume_pending=seek_requested=0;
    video_file_direction=0;
}

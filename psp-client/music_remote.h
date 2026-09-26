#ifndef PSPSTREAMER_MUSIC_REMOTE_H
#define PSPSTREAMER_MUSIC_REMOTE_H

/* One producer (HTTP worker), one consumer (music loop). Publish the action
 * last; do not overwrite it before consumption. No PCM/decoder ownership. */
enum { MUSIC_REMOTE_NONE, MUSIC_REMOTE_PAUSE, MUSIC_REMOTE_RESUME,
       MUSIC_REMOTE_STOP, MUSIC_REMOTE_SEEK, MUSIC_REMOTE_PLAY };
static volatile int music_remote_running, music_remote_action;
static volatile int music_remote_seconds;
static int music_remote_thread_id = -1;
/* One-slot metadata mailbox. The worker never touches GUI memory. */
static char music_radio_id[48], music_radio_station[192], music_radio_title[192];
static volatile int music_radio_ready;
/* Keep cancellation latency at the previous 500 ms while reducing TCP churn. */
static void remote_poll_wait(volatile int *running) {
    for(int i=0;i<4 && *running;i++)sceKernelDelayThread(500000);
}

static int music_remote_worker(SceSize args, void *argp) {
    int sequence = remote_control_sequence;
    (void)args; (void)argp;
    while (music_remote_running) {
        char path[704], reply[2048], action[16];
        int next, event = MUSIC_REMOTE_NONE;
        if (music_remote_action) { sceKernelDelayThread(10000); continue; }
        snprintf(path, sizeof(path), "/api/remote/next?after=%d%s%s", sequence,
                 music_radio_id[0]?"&radio=":"",music_radio_id);
#ifdef PSPSTREAMER_PLEX_REPORT
        plex_report_path(path,sizeof(path),sequence,0);
        if(music_radio_id[0]) {
            size_t used=strlen(path);
            snprintf(path+used,sizeof(path)-used,"&radio=%s",music_radio_id);
        }
#endif
        if (remote_http_get(path, reply, sizeof(reply), &music_remote_running) >= 0 &&
            music_remote_running && json_value(reply, "action", action, sizeof(action))) {
            if (remote_state_reset(reply, &sequence)) continue;
            if(music_radio_id[0] && !music_radio_ready &&
               json_value(reply,"radio_station",music_radio_station,sizeof(music_radio_station))) {
                music_radio_title[0]=0;
                json_value(reply,"radio_title",music_radio_title,sizeof(music_radio_title));
                __sync_synchronize();music_radio_ready=1;
            }
            next = json_integer(reply, "seq", sequence);
            if (next > sequence) {
                /* Leave Play unconsumed for the library dispatcher, which
                 * validates metadata and can start either music or video. */
                if (!strcmp(action, "play")) {
                    music_remote_action = MUSIC_REMOTE_PLAY;
                    break;
                }
                sequence = remote_control_sequence = next;
                if (!strcmp(action, "pause")) event = MUSIC_REMOTE_PAUSE;
                else if (!strcmp(action, "resume")) event = MUSIC_REMOTE_RESUME;
                else if (!strcmp(action, "stop")) event = MUSIC_REMOTE_STOP;
                else if (!strcmp(action, "seek")) {
                    int seconds = json_integer(reply, "seconds", -1);
                    if (seconds >= 0 && seconds <= 86400) {
                        music_remote_seconds = seconds;
                        event = MUSIC_REMOTE_SEEK;
                    }
                }
                music_remote_action = event;
            }
        }
        remote_poll_wait(&music_remote_running);
    }
#ifdef PSPSTREAMER_PLEX_REPORT
    plex_report_stop(sequence);
#endif
    return 0;
}

static int music_remote_start(void) {
    int result;
    music_remote_action = MUSIC_REMOTE_NONE;
    music_remote_seconds = -1;
    music_remote_running = 1;
    /* Full TLS handshakes recur on each remote poll. At the UI's 0x40
     * priority, their CPU work can stall both spectrum and MilkDrop while
     * the higher-priority DAC stays clean. Let the display preempt HTTPS
     * control work; it still runs during the UI's regular sleep intervals. */
    music_remote_thread_id = sceKernelCreateThread("PSPStreamerMusicRemote",
        music_remote_worker, server_https?0x41:0x40, server_https?0x10000:0x4000, 0, NULL);
    if (music_remote_thread_id < 0) {
        result = music_remote_thread_id;
        music_remote_thread_id = -1; music_remote_running = 0;
        return result;
    }
    result = sceKernelStartThread(music_remote_thread_id, 0, NULL);
    if (result < 0) {
        sceKernelDeleteThread(music_remote_thread_id);
        music_remote_thread_id = -1; music_remote_running = 0;
    }
    return result;
}

static void music_remote_stop(void) {
    music_remote_running = 0;
    if (music_remote_thread_id >= 0) {
        sceKernelWaitThreadEnd(music_remote_thread_id, NULL);
        sceKernelDeleteThread(music_remote_thread_id);
        music_remote_thread_id = -1;
    }
    music_remote_action = MUSIC_REMOTE_NONE;
}
#endif

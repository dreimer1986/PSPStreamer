/* Pre-playback HTTP: keep the menu alive while a cold original is probed or
 * subtitles are extracted. The worker owns the socket and response until it
 * has exited; cancellation must never kill a TLS-owning thread. */
#define MEDIA_REQUEST_CANCELLED (-1490)
static void media_wait_draw(int subtitles, unsigned int seconds, int cancelling);
static volatile int media_request_running, media_request_done;
static int media_request_result, media_request_budget, media_request_capacity;
static const char *media_request_path;
static const char *media_request_body;
static char *media_request_buffer;
static RemoteHttpReport media_request_report;
static int media_request_transient;

static int media_request_worker(SceSize args, void *argp) {
    (void)args; (void)argp;
    struct in_addr address;
    media_request_report.stage="DNS";
    if(resolve_server_address(&address)<0)media_request_result=-1004;
    else if(!media_request_running)media_request_result=MEDIA_REQUEST_CANCELLED;
    else media_request_result=remote_http_request_policy(media_request_path,media_request_buffer,
        media_request_capacity,&media_request_running,media_request_budget,media_request_body,&media_request_report);
    network_worker_finished("preparation");
    __sync_synchronize();
    media_request_done=1;
    return 0;
}

static int media_request_perform(const char *path,char *buffer,int capacity,int budget,int subtitles,const char *body) {
    /* A cancelled association may still own firmware networking. */
    if(wifi_worker_thread>=0) {media_request_transient=0;return MEDIA_REQUEST_CANCELLED;}
    SceCtrlData pad;
    int thread,cancelled=0;
    unsigned long long started=sceKernelGetSystemTimeWide(),redraw=0,logged=started;
    media_request_transient=0;
    media_request_report=(RemoteHttpReport){0,1,"starting"};
    recovery_log(subtitles?"subtitles begin":"metadata begin",0,0,"starting");
    media_request_path=path;media_request_buffer=buffer;
    media_request_body=body;
    media_request_capacity=capacity;media_request_budget=budget;
    media_request_running=1;media_request_done=0;
    thread=sceKernelCreateThread("media preparation",media_request_worker,
        0x41,0x10000,PSP_THREAD_ATTR_USER,NULL);
    if(thread<0) {media_request_running=0;return thread;}
    int result=sceKernelStartThread(thread,0,NULL);
    if(result<0) {sceKernelDeleteThread(thread);media_request_running=0;return result;}
    for(;;) {
        unsigned int timeout=1;
        if(media_request_done && sceKernelWaitThreadEnd(thread,&timeout)>=0)break;
        keep_awake();
        sceCtrlReadBufferPositive(&pad,1);
        if((pad.Buttons & PSP_CTRL_CIRCLE) || ((subtitles || music_transition) && (pad.Buttons & PSP_CTRL_START))) {cancelled=1;media_request_running=0;}
        unsigned long long now=sceKernelGetSystemTimeWide();
        if(now>=redraw) {
            media_wait_draw(subtitles,(unsigned int)((now-started)/1000000ULL),cancelled);
            redraw=now+150000ULL;
        }
        if(now-logged>=10000000ULL) {
            recovery_log("preparation waiting",0,media_request_report.status,media_request_report.stage);
            logged=now;
        }
        sceKernelDelayThread(20000);
    }
    sceKernelDeleteThread(thread);media_request_running=0;
    media_request_transient=!cancelled && media_request_result<0 && media_request_report.retryable;
    recovery_log(cancelled?"preparation cancelled":media_request_transient?"preparation retry":"preparation finished",
        cancelled?MEDIA_REQUEST_CANCELLED:media_request_result,media_request_report.status,media_request_report.stage);
    return cancelled?MEDIA_REQUEST_CANCELLED:media_request_result;
}
static int media_request_get(const char *path,char *buffer,int capacity,int budget,int subtitles) {
    return media_request_perform(path,buffer,capacity,budget,subtitles,NULL);
}

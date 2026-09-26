/* Only this worker enters association APIs. A cancelled/stalled call retains
 * ownership until it exits naturally; never terminate it or overlap resets. */
static volatile int wifi_worker_thread=-1;
static int wifi_worker_force,wifi_worker_result;
static volatile int wifi_worker_done;
static int wifi_worker_main(SceSize args,void *argp) {
    (void)args;(void)argp;
    wifi_worker_result=wifi_worker_force<0?wifi_initialize_core():wifi_associate_core(wifi_worker_force);
    __sync_synchronize();wifi_worker_done=1;
    return 0;
}
static int wifi_associate(int force) {
    network_ready=http_ready=0;
    unsigned int timeout=1;
    if(wifi_worker_thread>=0) {
        if(!wifi_worker_done || sceKernelWaitThreadEnd(wifi_worker_thread,&timeout)<0) {
            network_ready=http_ready=0;
            failure_step="WLAN busy - retry later";
            return -5;
        }
        sceKernelDeleteThread(wifi_worker_thread);wifi_worker_thread=-1;
    }
    wifi_worker_cancel=wifi_worker_done=0;wifi_worker_force=force;
    wifi_worker_step="WLAN worker";
    wifi_allow_apctl_restart=radio_connect_wait;
    wifi_worker_thread=sceKernelCreateThread("WLAN association",wifi_worker_main,0x41,0x6000,PSP_THREAD_ATTR_USER,NULL);
    if(wifi_worker_thread<0)return wifi_worker_thread;
    int result=sceKernelStartThread(wifi_worker_thread,0,NULL);
    if(result<0) {sceKernelDeleteThread(wifi_worker_thread);wifi_worker_thread=-1;return result;}
    unsigned long long started=sceKernelGetSystemTimeWide(),logged=started;
    for(;;) {
        timeout=1;
        if(wifi_worker_done && sceKernelWaitThreadEnd(wifi_worker_thread,&timeout)>=0)break;
        SceCtrlData pad;
        keep_awake();sceCtrlPeekBufferPositive(&pad,1);
        unsigned long long now=sceKernelGetSystemTimeWide();
        if((pad.Buttons & (PSP_CTRL_CIRCLE|PSP_CTRL_START)) || app_exit_requested || now-started>=60000000ULL) {
            wifi_worker_cancel=1;network_ready=http_ready=0;
            failure_step="WLAN cancelled - worker pending";
            recovery_log("WLAN worker detached",-5,0,wifi_worker_step);
            return -5;
        }
        if(now-logged>=10000000ULL) {
            recovery_log("WLAN worker waiting",0,0,wifi_worker_step);logged=now;
        }
        sceKernelDelayThread(20000);
    }
    __sync_synchronize();result=wifi_worker_result;failure_step=wifi_worker_step;
    sceKernelDeleteThread(wifi_worker_thread);wifi_worker_thread=-1;
    return force<0 && result==0?-1:result;
}

/* Single-owner directory request. The UI only reads the buffer after joining.
 * Cancellation never kills a thread that might own TLS/allocator locks. */
static int library_thread=-1, library_pending, library_result, library_cancelled;
static volatile int library_running, library_done;
static unsigned long long library_started;
static char library_request_path[ID_SIZE*3+32];
static char library_loaded_path[ID_SIZE];

static int library_worker(SceSize args,void *argp) {
    (void)args;(void)argp;
    library_result=library_fetch(library_request_path,&library_running);
    __sync_synchronize();library_done=1;
    return 0;
}
static void library_cancel(void) {
    library_running=0;library_cancelled=1;
}
/* 0: still working; 1: completed/cancelled; -1: no request. */
static int library_request_poll(void) {
    if(!library_pending)return -1;
    if(library_thread<0) {
        if(!browser_remote_stop())return 0;
        if(library_cancelled) {library_pending=0;return 1;}
        library_running=1;library_done=0;
        library_thread=sceKernelCreateThread("library request",library_worker,
            0x41,0x10000,PSP_THREAD_ATTR_USER,NULL);
        if(library_thread>=0) {
            library_result=sceKernelStartThread(library_thread,0,NULL);
            if(library_result>=0)return 0;
            sceKernelDeleteThread(library_thread);
        } else library_result=library_thread;
        library_thread=-1;library_running=0;library_pending=0;return 1;
    }
    if(!library_done)return 0;
    unsigned int timeout=1;
    if(sceKernelWaitThreadEnd(library_thread,&timeout)<0)return 0;
    sceKernelDeleteThread(library_thread);library_thread=-1;
    library_running=0;library_pending=0;
    return 1;
}

/* Idle-browser HTTP belongs to its own cancellable worker. Never share the
 * library response buffer or allow this worker to overlap playback/settings.
 * Only the UI thread consumes commands and changes remote_control_sequence. */
static int browser_remote_thread_id = -1;
static volatile int browser_remote_running, browser_remote_done;
static int browser_remote_result;
static char browser_remote_path[64], browser_remote_reply[2048];
static unsigned long long browser_remote_next;

static int browser_remote_worker(SceSize args, void *argp) {
    (void)args; (void)argp;
    browser_remote_result = remote_http_get(browser_remote_path, browser_remote_reply,
                                            sizeof(browser_remote_reply), &browser_remote_running);
    __sync_synchronize();
    browser_remote_done = 1;
    return 0;
}

static void browser_remote_stop(void) {
    browser_remote_running = 0;
    if (browser_remote_thread_id >= 0) {
        /* Cooperative cancellation: the socket/TLS state remains worker-owned. */
        sceKernelWaitThreadEnd(browser_remote_thread_id, NULL);
        sceKernelDeleteThread(browser_remote_thread_id);
        browser_remote_thread_id = -1;
    }
    browser_remote_done = 0;
}

/* Return a completed reply, otherwise schedule at most one request per second.
 * Callers must finish parsing the reply before calling this function again. */
static const char *browser_remote_poll(int sequence) {
    unsigned long long now = sceKernelGetSystemTimeWide();
    if (browser_remote_thread_id >= 0) {
        if (!browser_remote_done) return NULL;
        browser_remote_stop(); /* Join publishes the reply before parsing it. */
        browser_remote_next = now + 1000000ULL;
        return browser_remote_result >= 0 ? browser_remote_reply : NULL;
    }
    if (now < browser_remote_next) return NULL;
    browser_remote_next = now + 1000000ULL;
    snprintf(browser_remote_path, sizeof(browser_remote_path), "/api/remote/next?after=%d", sequence);
    browser_remote_running = 1;
    browser_remote_done = 0;
    browser_remote_thread_id = sceKernelCreateThread("browser remote", browser_remote_worker,
                                                    0x30, 0x8000, PSP_THREAD_ATTR_USER, NULL);
    if (browser_remote_thread_id < 0) { browser_remote_running = 0; return NULL; }
    if (sceKernelStartThread(browser_remote_thread_id, 0, NULL) < 0) {
        sceKernelDeleteThread(browser_remote_thread_id);
        browser_remote_thread_id = -1;
        browser_remote_running = 0;
    }
    return NULL;
}

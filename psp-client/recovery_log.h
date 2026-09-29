/* Download-only batching: callers never hold the lock during card I/O.
 * A separate writer flushes every five seconds and is joined on exit. Outside downloads
 * retain the original immediate durability. Never log credentials or URLs. */
static volatile int recovery_batch_active,recovery_batch_lock;
static char recovery_batch[4096];
static unsigned recovery_batch_size,recovery_batch_dropped;
static unsigned long long recovery_batch_next;
static SceUID recovery_writer=-1;
static volatile int recovery_writer_stop,recovery_writer_pending;
static unsigned long long recovery_flush_us,recovery_flush_max_us;
static unsigned recovery_flush_count;
static void recovery_log(const char *event,int result,int status,const char *stage);
static void recovery_write(const char *data,unsigned bytes) {
    SceUID fd=sceIoOpen("ms0:/PSP/SYSTEM/PSPStreamer-recovery.txt",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0600);
    if(fd<0)return;
    unsigned done=0;
    while(done<bytes){int n=sceIoWrite(fd,data+done,bytes-done);if(n<=0)break;done+=n;}
    sceIoClose(fd);
}
static void recovery_flush(void) {
    char batch[4224];unsigned bytes=0,dropped=0;
    /* Short RAM-only critical section. A busy producer will be drained next time. */
    if(__sync_lock_test_and_set(&recovery_batch_lock,1))return;
    bytes=recovery_batch_size;memcpy(batch,recovery_batch,bytes);recovery_batch_size=0;
    dropped=__sync_lock_test_and_set(&recovery_batch_dropped,0);
    __sync_lock_release(&recovery_batch_lock);
    if(dropped)bytes+=snprintf(batch+bytes,sizeof(batch)-bytes,"event=diagnostic overflow stage=dropped=%u\n",dropped);
    if(bytes) {
        unsigned long long begin=sceKernelGetSystemTimeWide();
        recovery_write(batch,bytes);
        unsigned long long elapsed=sceKernelGetSystemTimeWide()-begin;
        while(__sync_lock_test_and_set(&recovery_batch_lock,1))sceKernelDelayThread(1000);
        recovery_flush_us+=elapsed;
        if(elapsed>recovery_flush_max_us)recovery_flush_max_us=elapsed;
        recovery_flush_count++;
        __sync_lock_release(&recovery_batch_lock);
    }
}
static int recovery_writer_main(SceSize args,void *argp) {
    (void)args;(void)argp;
    while(!recovery_writer_stop) {
        if(__sync_lock_test_and_set(&recovery_writer_pending,0))recovery_flush();
        sceKernelDelayThread(10000);
    }
    return 0;
}
static void recovery_flush_due(void) {
    unsigned long long now=sceKernelGetSystemTimeWide();
    if(recovery_batch_active && now>=recovery_batch_next){
        recovery_batch_next=now+5000000ULL;
        if(recovery_writer>=0)__sync_lock_test_and_set(&recovery_writer_pending,1);
        else recovery_flush(); /* Safe synchronous fallback if thread creation failed. */
    }
}
static void recovery_batch_begin(void){
    if(!debug_enabled)return;
    recovery_flush_us=recovery_flush_max_us=0;recovery_flush_count=0;
    recovery_writer_stop=recovery_writer_pending=0;
    recovery_batch_next=sceKernelGetSystemTimeWide()+5000000ULL;recovery_batch_active=1;
    recovery_writer=sceKernelCreateThread("DownloadLog",recovery_writer_main,0x38,0x4000,0,NULL);
    if(recovery_writer>=0 && sceKernelStartThread(recovery_writer,0,NULL)<0){
        sceKernelDeleteThread(recovery_writer);recovery_writer=-1;
    }
}
static void recovery_batch_end(void){
    if(recovery_writer>=0){
        recovery_writer_stop=1;
        sceKernelWaitThreadEnd(recovery_writer,NULL);
        sceKernelDeleteThread(recovery_writer);recovery_writer=-1;
    }
    recovery_flush();
    /* Let an already-entered producer finish its RAM-only append. */
    while(__sync_lock_test_and_set(&recovery_batch_lock,1))sceKernelDelayThread(1000);
    recovery_batch_active=0;
    __sync_lock_release(&recovery_batch_lock);
    recovery_flush();
}
/* Called by the download worker only. Snapshot 64-bit counters under the RAM
 * lock: Allegrex cannot read them atomically while the writer updates them. */
static void recovery_download_timing(void) {
    char detail[144];unsigned long long total,maximum;unsigned count;
    if(!debug_enabled || __sync_lock_test_and_set(&recovery_batch_lock,1))return;
    total=recovery_flush_us;maximum=recovery_flush_max_us;count=recovery_flush_count;
    __sync_lock_release(&recovery_batch_lock);
    snprintf(detail,sizeof(detail),"flush_ms=%llu max_us=%llu count=%u async=%d session_cumulative=1",
        total/1000,maximum,count,recovery_writer>=0);
    recovery_log("download log IO",0,0,detail);
}
static void recovery_log(const char *event,int result,int status,const char *stage) {
    if(!debug_enabled)return;
    char line[256];
    int n=snprintf(line,sizeof(line),"tick_ms=%llu event=%s result=%d http=%d stage=%s\n",
        (unsigned long long)sceKernelGetSystemTimeWide()/1000ULL,event,result,status,stage);
    if(n<0 || n>=(int)sizeof(line))return;
    if(recovery_batch_active) {
        if(__sync_lock_test_and_set(&recovery_batch_lock,1)){__sync_fetch_and_add(&recovery_batch_dropped,1);return;}
        if(!recovery_batch_active){__sync_lock_release(&recovery_batch_lock);recovery_write(line,n);return;}
        if((unsigned)n<=sizeof(recovery_batch)-recovery_batch_size){memcpy(recovery_batch+recovery_batch_size,line,n);recovery_batch_size+=n;}
        else __sync_fetch_and_add(&recovery_batch_dropped,1);
        __sync_lock_release(&recovery_batch_lock);
    } else recovery_write(line,n);
}

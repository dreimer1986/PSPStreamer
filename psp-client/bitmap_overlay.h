/* Two-bank, single-producer mailbox. No network/file I/O in the renderer.
 * Worker writes only the back bank; ready publishes it. Consumer swaps front
 * before clearing ready, so the worker cannot overwrite the displayed sprite. */
#define BITMAP_MAX_BYTES (512*1024)
static unsigned char *bitmap_banks[2];
static int bitmap_capacity,bitmap_bank_index[2],bitmap_worker_id=-1;
static volatile int bitmap_running,bitmap_ready,bitmap_front,bitmap_wanted;
static volatile unsigned char bitmap_attempted[960];

static int bitmap_payload_size(const BitmapCue *c) {
    if(c->start<0 || c->end<=c->start || c->width<=0 || c->height<=0 ||
       c->canvas_width<=0 || c->canvas_width>65535 || c->canvas_height<=0 || c->canvas_height>65535 ||
       c->x<0 || c->x>=c->canvas_width || c->y<0 || c->y>=c->canvas_height ||
       c->width>c->canvas_width-c->x || c->height>c->canvas_height-c->y)return 0;
    long long size=1024+(long long)c->width*c->height;
    return size<=BITMAP_MAX_BYTES?(int)size:0;
}

static int bitmap_worker(SceSize args,void *argp) {
    (void)args;(void)argp;
    while(bitmap_running) {
        __sync_synchronize();
        int index=bitmap_wanted;
        if(bitmap_ready || index<0 || index>=bitmap_cue_count || bitmap_attempted[index]) {
            sceKernelDelayThread(20000);continue;
        }
        bitmap_attempted[index]=1; /* At most one fetch per cue per playback. */
        int bank=1-bitmap_front,expected=bitmap_payload_size(&bitmap_cues[index]),got=-1;
        if(expected) {
            if(offline_active)got=offline_bitmap(index,bitmap_banks[bank],bitmap_capacity);
            else {
                char path[ID_SIZE+96];
                snprintf(path,sizeof(path),"/api/bitmap-sprite/%s?track=%d&cue=%d&lcd=1",audio_media_id,selected_subtitle_track,index);
                got=remote_http_get_budget(path,(char *)bitmap_banks[bank],bitmap_capacity,&bitmap_running,3000);
            }
        }
        if(!bitmap_running)break;
        if(expected && got==expected) {
            bitmap_bank_index[bank]=index;
            __sync_synchronize();bitmap_ready=1;
        } else {
            char detail[96];snprintf(detail,sizeof(detail),"cue=%d expected=%d received=%d; skipped for this playback",index,expected,got);
            recovery_log("bitmap skipped",got,0,detail);
        }
    }
    if(!offline_active)network_worker_finished("bitmap subtitles");
    return 0;
}

static void bitmap_stop(void) {
    bitmap_running=0;
    if(bitmap_worker_id>=0) {
        sceKernelWaitThreadEnd(bitmap_worker_id,NULL);
        sceKernelDeleteThread(bitmap_worker_id);bitmap_worker_id=-1;
    }
    free(bitmap_banks[0]);free(bitmap_banks[1]);bitmap_banks[0]=bitmap_banks[1]=NULL;
    bitmap_ready=0;bitmap_loaded_cue=-1;
}

/* Called only after AVC/staging allocations, preserving MPEG startup order. */
static void bitmap_start(void) {
    if(!bitmap_client_side || !bitmap_cues || bitmap_worker_id>=0)return;
    int largest=0;
    for(int i=0;i<bitmap_cue_count;i++) {
        int size=bitmap_payload_size(&bitmap_cues[i]);if(size>largest)largest=size;
    }
    if(!largest)return;
    bitmap_capacity=largest+4096; /* HTTP headers plus binary body and NUL. */
    bitmap_banks[0]=malloc(bitmap_capacity);bitmap_banks[1]=malloc(bitmap_capacity);
    if(!bitmap_banks[0] || !bitmap_banks[1]) {
        bitmap_stop();recovery_log("bitmap buffers unavailable",-1,0,"video continues without bitmap overlay");return;
    }
    memset((void *)bitmap_attempted,0,sizeof(bitmap_attempted));
    bitmap_front=bitmap_ready=0;bitmap_wanted=-1;bitmap_loaded_cue=-1;
    bitmap_bank_index[0]=bitmap_bank_index[1]=-1;bitmap_running=1;
    bitmap_worker_id=sceKernelCreateThread("bitmap subtitles",bitmap_worker,0x41,0x10000,PSP_THREAD_ATTR_USER,NULL);
    if(bitmap_worker_id<0 || sceKernelStartThread(bitmap_worker_id,0,NULL)<0) {
        if(bitmap_worker_id>=0){sceKernelDeleteThread(bitmap_worker_id);bitmap_worker_id=-1;}
        bitmap_stop();recovery_log("bitmap worker unavailable",-1,0,"video continues without bitmap overlay");
    }
}

static const unsigned char *bitmap_pixels(int position,int *cue_index) {
    int index=-1,next=-1;
    if(!bitmap_running)return NULL;
    for(int i=0;i<bitmap_cue_count;i++) {
        const BitmapCue *c=&bitmap_cues[i];
        if(index<0 && position>=c->start && position<c->end)index=i;
        if(c->start>position && (long long)c->start-position<=3000 &&
           (next<0 || c->start<bitmap_cues[next].start))next=i;
    }
    if(bitmap_ready) {
        __sync_synchronize();
        int bank=1-bitmap_front,ready_index=bitmap_bank_index[bank];
        if(ready_index==index) {
            bitmap_front=bank;bitmap_loaded_cue=index;
            __sync_synchronize();bitmap_ready=0;
        } else if(bitmap_cues[ready_index].end<=position) {
            __sync_synchronize();bitmap_ready=0; /* Expired while fetching. */
        }
    }
    bitmap_wanted=index>=0 && bitmap_loaded_cue!=index && !bitmap_attempted[index]?index:next;
    if(index<0 || bitmap_loaded_cue!=index)return NULL;
    *cue_index=index;return bitmap_banks[bitmap_front];
}

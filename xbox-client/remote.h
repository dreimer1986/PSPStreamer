/* Xbox-only mailbox, one bounded worker at most, polled every three seconds.
 * Parsing and execution are main-thread-only; no shared catalog JSON parser. */
static SDL_Thread *remote_thread;
static SDL_atomic_t remote_cancel,remote_done;
static char remote_body[8192],remote_path[1800];
static unsigned remote_sequence;static Uint32 remote_at;
static int remote_worker(void *unused){
    (void)unused;Http h;unsigned at=0;int n=0;
    if(http_request(&h,remote_path,&remote_cancel,NULL,5000)){
        while(at<sizeof(remote_body)-1&&(n=http_read(&h,remote_body+at,sizeof(remote_body)-1-at))>0)at+=n;
        if(n<0||at==sizeof(remote_body)-1)at=0;
    }
    remote_body[at]=0;http_close(&h);SDL_AtomicSet(&remote_done,1);return 0;
}
static int remote_poll(unsigned dialog,int secret){
    if(remote_thread&&SDL_AtomicGet(&remote_done)){SDL_WaitThread(remote_thread,NULL);remote_thread=NULL;return *remote_body!=0;}
    if(!remote_thread&&(Sint32)(SDL_GetTicks()-remote_at)>=0){
        remote_at=SDL_GetTicks()+3000;snprintf(remote_path,sizeof(remote_path),"/api/xbox/remote?after=%u&dialog=%u&secret=%d",remote_sequence,dialog,secret);
        if(playing&&!strncmp(media_id,"radio.",6)){
            char encoded[1600];if(url_encode(media_id,encoded,sizeof(encoded)))snprintf(remote_path,sizeof(remote_path),"/api/xbox/remote?after=%u&dialog=%u&secret=%d&radio=%s",remote_sequence,dialog,secret,encoded);
        }
        SDL_AtomicSet(&remote_done,0);remote_thread=SDL_CreateThreadWithStackSize(remote_worker,"remote",65536,NULL);
    }return 0;
}
static void remote_shutdown(void){SDL_AtomicSet(&remote_cancel,1);if(remote_thread)SDL_WaitThread(remote_thread,NULL);remote_thread=NULL;}

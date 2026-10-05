/* GPL-2.0-or-later. Reuse PSP's bounded millisecond text-page protocol.
 * HTTP and parsing belong to one cancellable worker; SDL rendering never does
 * I/O. Two pages bound memory independently of episode duration/cue count. */
typedef struct {int start_ms,end_ms;char text[160];} SubtitleCue;
#include "subtitle_pages.h"
static int subtitle_overlay=1;
static struct {
    SDL_Thread *thread;SDL_mutex *lock;
    SDL_atomic_t cancel,ready,position,failed;
    SubtitlePage *pages;int bank,client;
    unsigned start;int track;char token[4700];
} subtitles;

static int subtitle_fetch_page(SubtitlePage *page,int offset){
    char path[5000];Http h;
    if(offset<0)snprintf(path,sizeof(path),"/api/subtitles/%s?track=%d&tv=1&timebase=ms&page=1&at_ms=%u",subtitles.token,subtitles.track,subtitles.start);
    else snprintf(path,sizeof(path),"/api/subtitles/%s?track=%d&tv=1&timebase=ms&page=1&offset=%d",subtitles.token,subtitles.track,offset);
    if(!http_request(&h,path,&subtitles.cancel,NULL,180000))return 0;
    unsigned cap=64*1024,used=0;int n=0;char *body=malloc(cap);
    if(!body){http_close(&h);return 0;}
    while(used<cap-1&&(n=http_read(&h,body+used,cap-1-used))>0)used+=n;
    body[used]=0;http_close(&h);
    int ok=n==0&&used<cap-1&&subtitle_page_parse(body,page)&&
        (offset<0||page->offset==offset);
    free(body);return ok;
}
static int subtitle_worker(void *unused){
    (void)unused;
    subtitles.client=subtitle_fetch_page(subtitles.pages,-1);
    SDL_AtomicSet(&subtitles.ready,1);
    if(!subtitles.client)return 0; /* bitmap/unsupported/error: retain burn-in */
    int current=0;
    while(!SDL_AtomicGet(&subtitles.cancel)){
        SubtitlePage *page=subtitles.pages+current;
        if(page->next<0)break;
        int next=1-current,loaded=0;
        /* Retry page retrieval, never restart or disturb the media transport. */
        for(int attempt=0;attempt<3&&!SDL_AtomicGet(&subtitles.cancel);attempt++){
            if(subtitle_fetch_page(subtitles.pages+next,page->next)&&subtitles.pages[next].until>=page->until){loaded=1;break;}
            for(int i=0;i<50&&!SDL_AtomicGet(&subtitles.cancel);i++)SDL_Delay(20);
        }
        if(!loaded){if(!SDL_AtomicGet(&subtitles.cancel))SDL_AtomicSet(&subtitles.failed,1);break;}
        while(!SDL_AtomicGet(&subtitles.cancel)&&SDL_AtomicGet(&subtitles.position)<page->until)SDL_Delay(20);
        if(SDL_AtomicGet(&subtitles.cancel))break;
        SDL_LockMutex(subtitles.lock);subtitles.bank=next;SDL_UnlockMutex(subtitles.lock);
        current=next;
    }
    return 0;
}
static void subtitle_stop(void){
    SDL_AtomicSet(&subtitles.cancel,1);
    if(subtitles.thread)SDL_WaitThread(subtitles.thread,NULL);
    if(subtitles.lock)SDL_DestroyMutex(subtitles.lock);
    free(subtitles.pages);memset(&subtitles,0,sizeof(subtitles));
}
static void subtitle_start(const char *token,int track,unsigned start){
    subtitle_stop();
    if(!subtitle_overlay||track<0)return;
    subtitles.pages=calloc(2,sizeof(SubtitlePage));subtitles.lock=SDL_CreateMutex();
    if(!subtitles.pages||!subtitles.lock){subtitle_stop();return;}
    snprintf(subtitles.token,sizeof(subtitles.token),"%s",token);subtitles.track=track;subtitles.start=start;
    SDL_AtomicSet(&subtitles.position,(int)start);
    subtitles.thread=SDL_CreateThreadWithStackSize(subtitle_worker,"subtitles",65536,NULL);
    if(!subtitles.thread)subtitle_stop();
}
static int subtitle_client_ready(SDL_atomic_t *cancel){
    if(!subtitles.thread)return 0;
    while(!SDL_AtomicGet(&subtitles.ready)&&!SDL_AtomicGet(cancel))SDL_Delay(10);
    return !SDL_AtomicGet(cancel)&&subtitles.client;
}
static void subtitle_current(unsigned milliseconds,char text[160]){
    text[0]=0;
    if(!SDL_AtomicGet(&subtitles.ready)||!subtitles.client)return;
    SDL_AtomicSet(&subtitles.position,(int)milliseconds);
    SDL_LockMutex(subtitles.lock);
    SubtitlePage *page=subtitles.pages+subtitles.bank;
    for(int i=0;i<page->count;i++){
        SubtitleCue *cue=page->cues+i;
        if(milliseconds<(unsigned)cue->start_ms)break;
        if(milliseconds<(unsigned)cue->end_ms){memcpy(text,cue->text,160);break;}
    }
    SDL_UnlockMutex(subtitles.lock);
}

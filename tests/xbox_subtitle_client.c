/* Focused worker lifecycle/page ownership test; no Xbox or network required. */
#define _DEFAULT_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef struct {int value;} SDL_atomic_t;
static int SDL_AtomicGet(SDL_atomic_t *a){return __atomic_load_n(&a->value,__ATOMIC_SEQ_CST);}
static void SDL_AtomicSet(SDL_atomic_t *a,int n){__atomic_store_n(&a->value,n,__ATOMIC_SEQ_CST);}
typedef pthread_mutex_t SDL_mutex;
typedef struct {pthread_t id;int (*fn)(void*);void *arg;} SDL_Thread;
static SDL_mutex *SDL_CreateMutex(void){SDL_mutex *m=malloc(sizeof(*m));pthread_mutex_init(m,NULL);return m;}
static void SDL_DestroyMutex(SDL_mutex *m){pthread_mutex_destroy(m);free(m);}
#define SDL_LockMutex pthread_mutex_lock
#define SDL_UnlockMutex pthread_mutex_unlock
static void SDL_Delay(unsigned n){usleep(n*1000);}
static void *thread_run(void *p){SDL_Thread *t=p;t->fn(t->arg);return NULL;}
static SDL_Thread *SDL_CreateThreadWithStackSize(int(*f)(void*),const char*n,unsigned size,void *arg){
    (void)n;(void)size;SDL_Thread *t=malloc(sizeof(*t));*t=(SDL_Thread){.fn=f,.arg=arg};pthread_create(&t->id,NULL,thread_run,t);return t;
}
static void SDL_WaitThread(SDL_Thread *t,void *unused){(void)unused;pthread_join(t->id,NULL);free(t);}
typedef struct {const char *body;unsigned at,size;SDL_atomic_t *cancel;} Http;
static int mode;
static int http_request(Http *h,const char *path,SDL_atomic_t *cancel,const char *body,unsigned timeout){
    (void)body;(void)timeout;h->at=0;h->cancel=cancel;
    if(mode==2){while(!SDL_AtomicGet(cancel))SDL_Delay(1);return 0;}
    if(mode==3){
        static unsigned char sprite[1026];memset(sprite,0,sizeof(sprite));
        sprite[4]=sprite[5]=sprite[6]=sprite[7]=255;sprite[1024]=sprite[1025]=1;
        if(strstr(path,"bitmap-sprite")){h->body=(const char*)sprite;h->size=sizeof(sprite);return 1;}
        h->body=strstr(path,"bitmap-subtitles")?"{\"t\":\"pgs\",\"c\":[[100,1000,0,0,2,1,4,2]]}":"{\"t\":\"bitmap\",\"c\":[]}";
    }else if(mode==1)h->body="{\"t\":\"bitmap\",\"c\":[]}";
    else if(strstr(path,"offset=1"))h->body="{\"t\":\"text\",\"paged\":1,\"offset\":1,\"next\":-1,\"until\":2147483647,\"c\":[[2000,3000,\"Zwei|ÄÖÜ\"]]}";
    else h->body="{\"t\":\"text\",\"paged\":1,\"offset\":0,\"next\":1,\"until\":2000,\"c\":[[100,1000,\"Eins\"]]}";
    h->size=strlen(h->body);return 1;
}
static void http_close(Http *h){(void)h;}
static int http_read(Http *h,void *out,unsigned cap){
    if(SDL_AtomicGet(h->cancel))return -1;
    unsigned n=h->size-h->at;if(n>7)n=7;if(n>cap)n=cap;
    memcpy(out,h->body+h->at,n);h->at+=n;return n;
}
static int http_exact(Http *h,void *out,unsigned size){unsigned at=0;while(at<size){int n=http_read(h,(char*)out+at,size-at);if(n<=0)return 0;at+=n;}return 1;}
#include "../xbox-client/subtitle_client.h"
int main(void){
    SDL_atomic_t cancel={0};char text[160];
    subtitle_start("episode",0,0);assert(subtitle_client_ready(&cancel));
    subtitle_current(50,text);assert(!*text);
    subtitle_current(500,text);assert(!strcmp(text,"Eins"));
    subtitle_current(1500,text);assert(!*text);
    for(int i=0;i<100;i++){subtitle_current(2500,text);if(*text)break;SDL_Delay(2);}
    assert(!strcmp(text,"Zwei|ÄÖÜ"));subtitle_current(3000,text);assert(!*text);
    subtitle_stop();assert(!subtitles.pages&&!subtitles.thread&&!subtitles.lock);
    mode=1;subtitle_start("bitmap",0,0);assert(!subtitle_client_ready(&cancel));subtitle_stop();
    mode=2;subtitle_start("slow",0,0);SDL_Delay(5);subtitle_stop();
    mode=3;subtitle_start("pgs",0,0);assert(subtitle_client_ready(&cancel)&&subtitles.client==2);
    subtitle_current(500,text);assert(!*text);
    unsigned char yuy2[16];for(int i=0;i<16;i+=2){yuy2[i]=16;yuy2[i+1]=128;}
    subtitle_bitmap_present(yuy2,8,4,2);assert(yuy2[0]==235&&yuy2[2]==235&&yuy2[4]==16);
    subtitle_stop();assert(!subtitles.bitmap_cues&&!subtitles.bitmap_slots[0].pixels);
    mode=0;subtitle_start("restart",0,0);assert(subtitle_client_ready(&cancel));
    subtitle_current(500,text);assert(!strcmp(text,"Eins"));subtitle_stop();
    puts("Xbox subtitles: pages, UTF-8, cue expiry, fallback, cancellation and restart passed");
}

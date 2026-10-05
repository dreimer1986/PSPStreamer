#define PL_MPEG_IMPLEMENTATION
#define PLM_NO_STDIO
#include "vendor/pl_mpeg.h"
#include "audio.h"
#include "mpeg_video.h"
#include "video_overlay.h"
#define PACKET_LIMIT (1024*1024)
#define QUEUE_BYTES (1536*1024)
typedef struct {unsigned char *data;unsigned size;int64_t pts;} Packet;
typedef struct {Packet p[64];unsigned read,count;} Packets;
typedef struct {
    SDL_mutex *lock;SDL_Thread *thread;SDL_atomic_t cancel,done,ready;
    Packets video,audio;unsigned bytes,flags;int ended;char error[120],path[6000];
} Stream;
static Stream stream;
static plm_buffer_t *audio_buffer;static plm_audio_t *audio_decoder;
static XboxVideo video;static unsigned char *video_input;
static plm_frame_t video_frame,*next_frame;static int64_t frame_pts;
static int video_codec=1; /* 0=MPEG-1, 1=MPEG-2 */
static int audio_matrix;
static int audio_quality=1;
static const char *audio_quality_keys[]={"128k","192k","256k","320k","384k"};
static const char *matrix_keys[]={"none","dolby","dplii"};
static const char *matrix_names[]={"Stereo","Dolby Surround","Dolby Pro Logic II"};
static const char *quality_names[]={"480x272","640x360","720x480 (16:9)","720x576 (16:9)","1280x720 (HD)","1920x1080 (HD)"};
static const char *quality_keys[]={"480p-low","360p","480p","576p","720p","1080p"};
static int display_wide=1;
static SDL_Texture *video_texture;static int texture_w,texture_h,playing,paused,decoder_ended,stream_video,stream_audio;
static Uint32 silent_start,log_time;static int64_t silent_pts;
static double seek_base,paused_position;static unsigned rendered,dropped,underflows;static int underrun,audio_finished;
extern Uint64 xbox_fb_copy_bytes;
extern unsigned xbox_fb_copy_calls;
static char player_diagnostic[640];
static int diagnostics_enabled=1;
static unsigned video_decode_ms;

static void stream_error(const char *s){SDL_LockMutex(stream.lock);snprintf(stream.error,sizeof(stream.error),"%s",s);SDL_UnlockMutex(stream.lock);}
static void stream_error_if_empty(const char *s){SDL_LockMutex(stream.lock);if(!*stream.error)snprintf(stream.error,sizeof(stream.error),"%s",s);SDL_UnlockMutex(stream.lock);}
static int queue_packet(Packets *q,Packet p){
    while(!SDL_AtomicGet(&stream.cancel)){
        SDL_LockMutex(stream.lock);
        if(q->count<64&&stream.bytes+p.size<=QUEUE_BYTES){q->p[(q->read+q->count)%64]=p;q->count++;stream.bytes+=p.size;SDL_UnlockMutex(stream.lock);return 1;}
        SDL_UnlockMutex(stream.lock);SDL_Delay(2);
    }return 0;
}
static int pop_packet(Packets *q,Packet *p){
    SDL_LockMutex(stream.lock);int found=q->count>0;
    if(found){*p=q->p[q->read];q->read=(q->read+1)%64;q->count--;stream.bytes-=p->size;}
    SDL_UnlockMutex(stream.lock);return found;
}
static unsigned queue_count(Packets *q){SDL_LockMutex(stream.lock);unsigned count=q->count;SDL_UnlockMutex(stream.lock);return count;}
static int network_stream(void *unused){
    (void)unused;Http h;unsigned char intro[8],header[16];
    if(!http_open(&h,stream.path,&stream.cancel)){char e[96];snprintf(e,sizeof(e),"Stream HTTP %d / network error",h.status);stream_error(e);goto done;}
    if(!http_exact(&h,intro,8)||memcmp(intro,"XSM1",4)){stream_error("Server 0.1.68 or newer required");goto close;}
    memcpy(&stream.flags,intro+4,4);
    if(!stream.flags||stream.flags>3){stream_error("Invalid stream flags");goto close;}
    SDL_AtomicSet(&stream.ready,1);
    while(!SDL_AtomicGet(&stream.cancel)){
        if(!http_exact(&h,header,16)){stream_error("Stream interrupted (no clean EOF)");break;}
        Packet p={0};memcpy(&p.size,header+4,4);memcpy(&p.pts,header+8,8);
        if(header[0]=='E'&&!p.size){stream.ended=1;break;}
        if((header[0]!='V'&&header[0]!='A')||!p.size||p.size>PACKET_LIMIT||p.pts<0){char e[120];snprintf(e,sizeof(e),"Invalid media packet kind=%u bytes=%u limit=%u",header[0],p.size,PACKET_LIMIT);stream_error(e);break;}
        p.data=malloc(p.size);if(!p.data){stream_error("Out of packet memory");break;}
        if(!http_exact(&h,p.data,p.size)){free(p.data);stream_error("Truncated media packet");break;}
        if(!queue_packet(header[0]=='V'?&stream.video:&stream.audio,p)){free(p.data);break;}
    }
close:http_close(&h);
done:SDL_AtomicSet(&stream.done,1);return 0;
}
static void player_stop(void){
    /* Release the visual GPU owner before reopening the PVIDEO path. */
    extern void xbox_visual_stop(void);xbox_visual_stop();
    overlay_close();
    if(stream.thread){SDL_AtomicSet(&stream.cancel,1);SDL_WaitThread(stream.thread,NULL);stream.thread=NULL;}
    if(stream.lock){Packet p;while(pop_packet(&stream.video,&p))free(p.data);while(pop_packet(&stream.audio,&p))free(p.data);SDL_DestroyMutex(stream.lock);stream.lock=NULL;}
    if(pcm&&!audio_reset())audio_failed=1;
    xbox_video_close(&video);free(video_input);video_input=NULL;
    if(audio_decoder)plm_audio_destroy(audio_decoder);audio_decoder=NULL;audio_buffer=NULL;
    if(video_texture)SDL_DestroyTexture(video_texture);video_texture=NULL;texture_w=texture_h=0;
    playing=paused=0;next_frame=NULL;decoder_ended=0;
}
static int player_start(double seconds){
    player_stop();if(!audio_init())return 0;
    xbox_fb_copy_bytes=0;xbox_fb_copy_calls=0;
    memset(&stream,0,sizeof(stream));stream.lock=SDL_CreateMutex();if(!stream.lock)return 0;
    char token[4700];if(!url_encode(media_id,token,sizeof(token))){player_stop();return 0;}
    unsigned start_ms=(unsigned)(seconds*1000);
    snprintf(stream.path,sizeof(stream.path),"/api/xbox-stream/%s?kind=%s&audio=%d&subtitle=%d&profile=tv&xbox_size=%s&xbox_codec=%s&xbox_matrix=%s&xbox_audio=%s&start=%u.%03u",token,media_audio?"audio":"video",audio_track,media_audio?-1:subtitle_track,quality_keys[quality],video_codec?"mpeg2":"mpeg1",matrix_keys[audio_matrix],audio_quality_keys[audio_quality],start_ms/1000,start_ms%1000);
    if(!xbox_video_init(&video)){player_stop();return 0;}
    audio_buffer=plm_buffer_create_with_capacity(4096);audio_decoder=plm_audio_create_with_buffer(audio_buffer,1);
    stream.thread=SDL_CreateThreadWithStackSize(network_stream,"stream",65536,NULL);if(!stream.thread){player_stop();return 0;}
    playing=1;seek_base=seconds;silent_start=0;silent_pts=-1;stream_audio=stream_video=0;audio_finished=0;rendered=dropped=underflows=0;video_decode_ms=0;underrun=0;log_time=SDL_GetTicks();return 1;
}
static int safe_video(const Packet *p){
    for(unsigned i=0;i+7<p->size;i++)if(!memcmp(p->data+i,"\0\0\1\xb3",4)){
        unsigned w=p->data[i+4]*16+(p->data[i+5]>>4),h=(p->data[i+5]&15)*256+p->data[i+6];
        if(!w||!h||w>1920||h>1088)return 0;
    }
    return 1;
}
static double player_position(void){if(paused)return paused_position;int64_t tick=stream_audio&&!audio_finished?audio_clock:silent_pts<0?0:silent_pts+(int64_t)(SDL_GetTicks()-silent_start)*90;return seek_base+(tick<0?0:(double)tick/90000);}
/* Release the encoder on pause, so a long pause cannot time out a live socket.
 * Resume starts a fresh timestamped stream at the recorded audio position. */
static void player_pause(void){if(paused){player_start(!strncmp(media_id,"radio.",6)?0:paused_position);}else{paused_position=player_position();player_stop();playing=paused=1;}}

/* Return 1 for a newly due video frame, 2 for EOF, -1 for a hard error. */
static int player_tick(void){
    if(paused)return 0;
    if(!SDL_AtomicGet(&stream.ready))return SDL_AtomicGet(&stream.done)?-1:0;
    stream_video=stream.flags&1;stream_audio=stream.flags&2;
    if(paused)return 0;
    Packet p;unsigned queued=audio_queued();
    while(stream_audio&&queued<24&&pop_packet(&stream.audio,&p)){
        plm_buffer_write(audio_buffer,p.data,p.size);free(p.data);
        plm_samples_t *s=plm_audio_decode(audio_decoder);
        if(!s||plm_audio_get_samplerate(audio_decoder)!=48000){stream_error("MP2 decode failed");return -1;}
        audio_submit(s,p.pts);queued++;
    }
    if(stream_audio&&SDL_AtomicGet(&stream.done)&&stream.ended&&!queue_count(&stream.audio)&&!audio_tail&&audio_sent){
        audio_submit_tail();queued++;
    }
    if(stream_audio&&!audio_running&&queued&&(queued>=16||SDL_AtomicGet(&stream.done))){audio_clock=audio_pts[0];audio_running=1;XAudioPlay();}
    if(audio_running){queued=audio_queued();if(!queued&&!SDL_AtomicGet(&stream.done)){if(!underrun)underflows++;underrun=1;}else underrun=0;}
    /* A genuinely shorter audio track must not freeze the last video frames.
     * Only hand off after network EOF and every audio sample has drained. */
    if(stream_audio&&!audio_finished&&SDL_AtomicGet(&stream.done)&&!queue_count(&stream.audio)&&!queued){
        audio_finished=1;silent_start=SDL_GetTicks();silent_pts=audio_clock<0?0:audio_clock;
    }
    for(int work=0;stream_video&&!next_frame&&work<4;work++){
        if(video.needs_input){
            free(video_input);video_input=NULL;
            if(pop_packet(&stream.video,&p)){
                if(!safe_video(&p)){free(p.data);stream_error("Unsupported MPEG dimensions");return -1;}
                video_input=p.data;xbox_video_feed(&video,p.data,p.size,p.pts);
            }else if(SDL_AtomicGet(&stream.done)){
                if(!stream.ended){stream_error_if_empty("Stream interrupted (MPEG)");return -1;}
                if(!video.finished)xbox_video_end(&video);
                else{decoder_ended=1;if(video.displayed!=video.submitted){stream_error("Incomplete MPEG stream at EOF");return -1;}break;}
            }else break;
        }
        Uint32 decode_start=SDL_GetTicks();int result=xbox_video_step(&video);video_decode_ms+=SDL_GetTicks()-decode_start;
        if(result<0){stream_error("MPEG decode: invalid data or insufficient RAM");return -1;}
        if(result>0){
            const mpeg2_sequence_t *s=video.info->sequence;const mpeg2_fbuf_t *f=video.info->display_fbuf;
            video_frame.width=s->picture_width;video_frame.height=s->picture_height;
            video_frame.y.data=f->buf[0];video_frame.y.width=s->width;
            video_frame.cb.data=f->buf[1];video_frame.cb.width=s->chroma_width;
            video_frame.cr.data=f->buf[2];video_frame.cr.width=s->chroma_width;
            next_frame=&video_frame;frame_pts=video.pts;
        }
    }
    if(!stream_audio&&next_frame&&!silent_start){silent_start=SDL_GetTicks();silent_pts=frame_pts;}
    int64_t now=stream_audio&&!audio_finished?audio_clock:silent_pts+(int64_t)(SDL_GetTicks()-silent_start)*90;
    if(next_frame&&(!stream_audio||audio_running||audio_finished)&&!underrun&&frame_pts<=now){
        if(now-frame_pts>9000){dropped++;next_frame=NULL;return 0;}
        rendered++;return 1;
    }
    if(diagnostics_enabled&&SDL_GetTicks()-log_time>5000){
        SDL_LockMutex(stream.lock);unsigned bytes=stream.bytes;SDL_UnlockMutex(stream.lock);
        snprintf(player_diagnostic,sizeof(player_diagnostic),"pos_ms=%u vpts=%lld apts=%lld shown=%u dropped=%u underruns=%u audio_queue=%u net_bytes=%u renderer=%s gpu_frames=%u gpu_busy=%u decode_ms=%u pack_ms=%u gui_ms=%u gui_kib=%u gui_copies=%u net_done=%d clean=%d decoder_end=%d pending_frame=%d analog=%u/%u/%u/%u digital=%u/%u/%u/%u\n",(unsigned)(player_position()*1000),(long long)frame_pts,(long long)audio_clock,rendered,dropped,underflows,queued,bytes,overlay.active&&!overlay.disabled?"nv2a":"software",overlay.shown,overlay.busy,video_decode_ms,overlay.pack_ms,overlay.gui_ms,(unsigned)(xbox_fb_copy_bytes/1024),xbox_fb_copy_calls,SDL_AtomicGet(&stream.done),stream.ended,decoder_ended,next_frame!=NULL,ac97[0x114],ac97[0x115],ac97[0x116],*(volatile unsigned short*)(ac97+0x118),ac97[0x174],ac97[0x175],ac97[0x176],*(volatile unsigned short*)(ac97+0x178));log_time=SDL_GetTicks();
    }
    if(SDL_AtomicGet(&stream.done)&&!queue_count(&stream.video)&&!queue_count(&stream.audio)&&(!stream_video||decoder_ended)&&!next_frame&&!queued)return stream.ended?2:-1;
    return 0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later
 * One incremental libmpeg2 decoder for MPEG-1/2. No frame-count clock.
 * Caller retains each input until needs_input, and each picture until release.
 */
#include "vendor/libmpeg2/mpeg2.h"
typedef struct {
    mpeg2dec_t *decoder;
    int needs_input,finished,failed;
    unsigned submitted,displayed;
    const mpeg2_info_t *info;
    int64_t pts;
    unsigned char *buffers[3];
} XboxVideo;
static int xbox_video_init(XboxVideo *v){
    memset(v,0,sizeof(*v));
    mpeg2_accel(MPEG2_ACCEL_X86_MMX|MPEG2_ACCEL_X86_MMXEXT);
    v->decoder=mpeg2_init();v->needs_input=1;
    if(v->decoder)v->info=mpeg2_info(v->decoder);
    return v->decoder!=NULL;
}
static void xbox_video_feed(XboxVideo *v,unsigned char *data,unsigned size,int64_t pts){
    mpeg2_tag_picture(v->decoder,(uint32_t)pts,(uint32_t)((uint64_t)pts>>32));
    mpeg2_buffer(v->decoder,data,data+size);v->needs_input=0;v->submitted++;
}
static void xbox_video_end(XboxVideo *v){
    /* A sequence-end sentinel completes the final slice AND releases the last
     * reference picture. Never synthesize this for an interrupted connection. */
    static unsigned char end[]={0,0,1,0xb7};
    mpeg2_buffer(v->decoder,end,end+sizeof(end));v->needs_input=0;v->finished=1;
}
static int xbox_video_step(XboxVideo *v){
    while(!v->needs_input){
        mpeg2_state_t state=mpeg2_parse(v->decoder);
        if(state==STATE_BUFFER){v->needs_input=1;return 0;}
        if(state==STATE_INVALID){v->failed=1;return -1;}
        if(state==STATE_SEQUENCE||state==STATE_SEQUENCE_MODIFIED||state==STATE_SEQUENCE_REPEATED){
            const mpeg2_sequence_t *s=v->info->sequence;
            if(!s||!s->width||!s->height||s->width>1920||s->height>1088||
               s->chroma_width!=s->width/2||s->chroma_height!=s->height/2){v->failed=1;return -1;}
            if(state==STATE_SEQUENCE){
                unsigned y=s->width*s->height,uv=s->chroma_width*s->chroma_height;
                for(int i=0;i<3;i++){
                    mpeg2_free(v->buffers[i]);v->buffers[i]=mpeg2_malloc(y+2*uv,MPEG2_ALLOC_YUV);
                    if(!v->buffers[i]){v->failed=1;return -1;}
                    unsigned char *planes[]={v->buffers[i],v->buffers[i]+y,v->buffers[i]+y+uv};
                    mpeg2_set_buf(v->decoder,planes,NULL);
                }
            }
        }
        if((state==STATE_SLICE||state==STATE_END||state==STATE_INVALID_END)&&v->info->display_fbuf){
            const mpeg2_picture_t *p=v->info->display_picture;
            if(!p||!(p->flags&PIC_FLAG_TAGS)){v->failed=1;return -1;}
            v->pts=(int64_t)(((uint64_t)p->tag2<<32)|p->tag);v->displayed++;return 1;
        }
    }return 0;
}
static void xbox_video_close(XboxVideo *v){if(v->decoder)mpeg2_close(v->decoder);for(int i=0;i<3;i++)mpeg2_free(v->buffers[i]);memset(v,0,sizeof(*v));}

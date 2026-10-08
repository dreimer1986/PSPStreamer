/* SPDX-License-Identifier: GPL-2.0-or-later
 * IEC60958 transmitter. I2S is only a bit serializer: no DAC or I2S receiver
 * is connected to this pin. DMA completion, not USB acceptance, is the clock.
 */
#include "spdif.h"
#include "trace.h"
#include "esp_timer.h"
#include "board.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include <stdlib.h>

#if SM_GENERIC_BOARD
int sm_audio_command(const SmFrame *request,SmFrame *reply) {
    (void)request;(void)reply;return SM_INVALID;
}
void sm_audio_disconnect(void) {}
void sm_trace_audio(SmTrace *out){(void)out;}
#else
#define RING_FRAMES 16384U
/* A full IEC60958 status block need not fit one DMA descriptor. Keep each
 * allocation below 1 KiB plus allocator metadata; the observed fragmented
 * internal heap could no longer supply the former 3 KiB buffers.
 * Six buffers retain 8 ms of DMA runway at 48 kHz. */
#define BLOCK_FRAMES 64U
#define DMA_BLOCKS 6U
_Static_assert((BLOCK_FRAMES*DMA_BLOCKS)%192==0,"DMA ring preserves IEC60958 status phase");
typedef struct {uint32_t stereo,end_pts_ms;} Sample;
typedef struct {void *buffer;uint32_t count,end_pts;} Completion;
static i2s_chan_handle_t output;
static int output_ready,output_running;
static Sample *ring;
static Completion dma[DMA_BLOCKS];
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static uint32_t rd,wr,rate,non_audio,session,sequence,completed,position,underruns;
static unsigned frame,level,paused,disconnected,playing;
static uint16_t bmc[256];
static SmAudioDiag diagnostic;
static uint32_t dma_callbacks,max_dma_gap_us,last_dma_us;
void sm_trace_audio(SmTrace *out) {
    portENTER_CRITICAL(&lock);
    out->audio_session=session;out->audio_rate=rate;
    out->audio_flags=(paused?1:0)|(disconnected?2:0)|(playing?4:0);
    out->audio_queued=wr-rd;out->audio_completed=completed;out->audio_underruns=underruns;
    out->dma_callbacks=dma_callbacks;out->max_dma_gap_us=max_dma_gap_us;
    portEXIT_CRITICAL(&lock);
}
static void diagnose(unsigned stage,esp_err_t error) {
    diagnostic.stage=stage;diagnostic.error=error;
    diagnostic.dma_free=heap_caps_get_free_size(MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
    diagnostic.dma_largest=heap_caps_get_largest_free_block(MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
    diagnostic.psram_free=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

/* Encoded bytes are sent MSB first by the I2S serializer. Sample bits are
 * consumed LSB first, with a transition at each cell boundary. */
static uint16_t encode_byte(unsigned value) {
    unsigned out=0,state=0;
    for(unsigned i=0;i<8;i++) {
        state^=1;out=(out<<1)|state;
        state^=(value>>i)&1;out=(out<<1)|state;
    }
    return (uint16_t)out;
}
static void subframe(uint32_t *dst,uint16_t sample,unsigned right) {
    unsigned c=0;
    if(frame==1)c=non_audio; /* IEC60958 consumer non-audio bit. */
    if(frame>=24 && frame<28) {
        unsigned code=rate==48000?2:rate==32000?3:0;
        c=(code>>(frame-24))&1;
    }
    /* IEC61937-1 6.1.3: V=1 protects PCM-only receivers before the complete
     * non-audio channel-status block has arrived. */
    uint32_t bits=((uint32_t)sample<<8)|(non_audio<<24)|(c<<26);
    bits|=(uint32_t)(__builtin_parity(bits)&1)<<27;
    unsigned preamble=right?0xe4:frame?0xe2:0xe8;
    if(level)preamble^=0xff;
    level=preamble&1;
    uint64_t word=preamble;
    for(unsigned i=0;i<3;i++) {
        unsigned v=bmc[(bits>>(8*i))&255]^(level?0xffff:0);
        word=(word<<16)|v;level=v&1;
    }
    unsigned tail=bmc[(bits>>24)&15]>>8;
    if(level)tail^=255;
    word=(word<<8)|tail;level=tail&1;
    dst[0]=(uint32_t)(word>>32);dst[1]=(uint32_t)word;
}
static bool sent(i2s_chan_handle_t channel,i2s_event_data_t *event,void *unused) {
    (void)channel;(void)unused;
    Completion *slot=NULL;
    for(unsigned i=0;i<DMA_BLOCKS;i++)if(dma[i].buffer==event->dma_buf){slot=&dma[i];break;}
    if(!slot)for(unsigned i=0;i<DMA_BLOCKS;i++)if(!dma[i].buffer){slot=&dma[i];slot->buffer=event->dma_buf;break;}
    if(!slot || event->size!=BLOCK_FRAMES*16)return false;
    portENTER_CRITICAL_ISR(&lock);
    uint32_t now=(uint32_t)esp_timer_get_time();
    if(last_dma_us && now-last_dma_us>max_dma_gap_us)max_dma_gap_us=now-last_dma_us;
    last_dma_us=now;dma_callbacks++;
    completed+=slot->count;
    if(slot->count){position=slot->end_pts;playing=1;}
    slot->count=0;
    uint32_t *out=event->dma_buf;
    int starved=0;
    for(unsigned i=0;i<BLOCK_FRAMES;i++) {
        uint32_t sample=0;
        if(!paused && !disconnected && rd!=wr) {
            Sample s=ring[rd++%RING_FRAMES];sample=s.stereo;
            slot->count++;slot->end_pts=s.end_pts_ms;
        } else if(!paused && !disconnected && playing)starved=1;
        /* Non-audio zero words are IEC61937 stuffing, not PCM. The non-audio
         * channel-status flag remains set even during pause/underflow. */
        subframe(out+i*4,(uint16_t)sample,0);
        subframe(out+i*4+2,(uint16_t)(sample>>16),1);
        frame=(frame+1)%192;
    }
    if(starved)underruns++;
    portEXIT_CRITICAL_ISR(&lock);
    return false;
}
void sm_audio_disconnect(void) {
    portENTER_CRITICAL(&lock);disconnected=1;paused=1;portEXIT_CRITICAL(&lock);
}
static void stop_output(void) {
    /* Keep the bounded opt-in DMA allocation across tracks. Reallocating it
     * amid Wi-Fi/BT activity can fail even after hours of successful audio. */
    portENTER_CRITICAL(&lock);paused=1;disconnected=1;portEXIT_CRITICAL(&lock);
    if(output_running){i2s_channel_disable(output);output_running=0;}
    memset(dma,0,sizeof(dma));
}
static void close_output(void) {
    stop_output();
    if(output){i2s_del_channel(output);output=NULL;}
    output_ready=0;
#if !SM_GENERIC_BOARD
    gpio_set_direction(GPIO_NUM_12,GPIO_MODE_OUTPUT);gpio_set_level(GPIO_NUM_12,0);
#endif
    free(ring);ring=NULL;
    memset(dma,0,sizeof(dma));
}
static int open_output(const SmAudioOpen *request) {
    diagnostic=(SmAudioDiag){.rate=request->rate,.non_audio=request->non_audio};
    diagnose(SM_AUDIO_STAGE_VALIDATE,ESP_ERR_INVALID_ARG);
    if((request->rate!=32000 && request->rate!=44100 && request->rate!=48000)||request->non_audio>1)return SM_INVALID;
    stop_output();
    unsigned stage=SM_AUDIO_STAGE_RING;
    esp_err_t err=ESP_ERR_NO_MEM;
    if(!ring)ring=heap_caps_malloc(sizeof(*ring)*RING_FRAMES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!ring)goto fail;
    for(unsigned i=0;i<256;i++)bmc[i]=encode_byte(i);
    rate=request->rate;non_audio=request->non_audio;
    rd=wr=sequence=completed=position=underruns=frame=level=playing=disconnected=0;
    dma_callbacks=max_dma_gap_us=last_dma_us=0;
    paused=1;session++;if(!session)session++;
    i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO,I2S_ROLE_MASTER);
    channel.dma_desc_num=DMA_BLOCKS;channel.dma_frame_num=BLOCK_FRAMES*2;
    stage=SM_AUDIO_STAGE_CHANNEL;
    if(!output && (err=i2s_new_channel(&channel,&output,NULL))!=ESP_OK)goto fail;
    i2s_std_config_t config={
        .clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(rate*2),
        .slot_cfg=I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,I2S_SLOT_MODE_STEREO),
        .gpio_cfg={.mclk=I2S_GPIO_UNUSED,.bclk=I2S_GPIO_UNUSED,.ws=I2S_GPIO_UNUSED,
                   .dout=GPIO_NUM_12,.din=I2S_GPIO_UNUSED}
    };
    stage=SM_AUDIO_STAGE_MODE;
    if(output_ready)err=i2s_channel_reconfig_std_clock(output,&config.clk_cfg);
    else {
        err=i2s_channel_init_std_mode(output,&config);
        if(err==ESP_OK)output_ready=1;
    }
    if(err!=ESP_OK)goto fail;
    i2s_event_callbacks_t callbacks={.on_sent=sent};
    stage=SM_AUDIO_STAGE_CALLBACK;
    if((err=i2s_channel_register_event_callback(output,&callbacks,NULL))!=ESP_OK)goto fail;
    /* Preload valid silence instead of initially emitting unframed zero bits. */
    stage=SM_AUDIO_STAGE_SILENCE;err=ESP_ERR_NO_MEM;
    uint32_t *silence=heap_caps_malloc(BLOCK_FRAMES*16,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!silence)goto fail;
    stage=SM_AUDIO_STAGE_PRELOAD;err=ESP_OK;
    for(unsigned i=0;i<DMA_BLOCKS;i++) {
        /* Each 64-frame chunk carries a different third of the 192-frame
         * channel-status block. Repeating the first chunk would corrupt the
         * preambles/rate/non-audio flags, especially in passthrough mode. */
        for(unsigned j=0;j<BLOCK_FRAMES;j++) {
            subframe(silence+j*4,0,0);subframe(silence+j*4+2,0,1);frame=(frame+1)%192;
        }
        size_t loaded=0;err=i2s_channel_preload_data(output,silence,BLOCK_FRAMES*16,&loaded);
        diagnostic.loaded+=loaded;
        if(err!=ESP_OK || loaded!=BLOCK_FRAMES*16){if(err==ESP_OK)err=ESP_FAIL;break;}
    }
    free(silence);
    if(err!=ESP_OK)goto fail;
    stage=SM_AUDIO_STAGE_ENABLE;
    if((err=i2s_channel_enable(output))!=ESP_OK)goto fail;
    output_running=1;
    diagnose(SM_AUDIO_STAGE_READY,ESP_OK);
    return SM_OK;
fail:
    diagnose(stage,err);
    ESP_LOGE("spdif","open stage=%u error=%s (%d) rate=%lu DMA free=%lu largest=%lu PSRAM=%lu loaded=%lu",
             stage,esp_err_to_name(err),err,(unsigned long)diagnostic.rate,
             (unsigned long)diagnostic.dma_free,(unsigned long)diagnostic.dma_largest,
             (unsigned long)diagnostic.psram_free,(unsigned long)diagnostic.loaded);
    close_output();return SM_IO;
}
int sm_audio_command(const SmFrame *r,SmFrame *reply) {
    int result=SM_OK;
    if(r->op==SM_AUDIO_DIAG) {
        if(r->length)return SM_INVALID;
        memcpy(reply->payload,&diagnostic,sizeof(diagnostic));reply->length=sizeof(diagnostic);
        return SM_OK;
    }
    if(r->op==SM_AUDIO_OPEN) {
        SmAudioOpen request;
        if(r->length!=sizeof(request))return SM_INVALID;
        memcpy(&request,r->payload,sizeof(request));result=open_output(&request);
        sm_trace_event(SM_TRACE_AUDIO_OPEN,request.rate,(unsigned)result);
    } else {
        uint32_t id;
        if(r->length<sizeof(id))return SM_INVALID;
        memcpy(&id,r->payload,sizeof(id));
        if(!output || id!=session || disconnected)return SM_OFFLINE;
        if(r->op==SM_AUDIO_CLOSE){sm_trace_event(SM_TRACE_AUDIO_CLOSE,session,0);stop_output();return SM_OK;}
        if(r->op==SM_AUDIO_PAUSE) {
            SmAudioPause request;
            if(r->length!=sizeof(request))return SM_INVALID;
            memcpy(&request,r->payload,sizeof(request));if(request.paused>1)return SM_INVALID;
            portENTER_CRITICAL(&lock);paused=request.paused;portEXIT_CRITICAL(&lock);
        } else if(r->op==SM_AUDIO_WRITE) {
            SmAudioWrite request;
            if(r->length<sizeof(request))return SM_INVALID;
            memcpy(&request,r->payload,sizeof(request));
            if(!request.frames || request.frames>(SM_PAYLOAD_SIZE-sizeof(request))/4 ||
               r->length!=sizeof(request)+request.frames*4 || request.sequence!=sequence)return SM_INVALID;
            portENTER_CRITICAL(&lock);
            if(request.frames>RING_FRAMES-(wr-rd))result=SM_BUSY;
            portEXIT_CRITICAL(&lock);
            if(result==SM_OK) {
                /* One command worker produces; ISR only consumes published
                 * entries. Fill free PSRAM outside the interrupt lock, then
                 * publish atomically. No per-sample 64-bit division in a
                 * critical section competing with Wi-Fi/USB interrupts. */
                uint32_t stamp=request.pts_ms,remainder=0;
                for(unsigned i=0;i<request.frames;i++) {
                    Sample *s=&ring[(wr+i)%RING_FRAMES];
                    memcpy(&s->stereo,r->payload+sizeof(request)+i*4,4);
                    remainder+=1000;
                    if(remainder>=rate){remainder-=rate;stamp++;}
                    s->end_pts_ms=stamp;
                }
                portENTER_CRITICAL(&lock);
                wr+=request.frames;sequence++;
                portEXIT_CRITICAL(&lock);
            }
        } else if(r->op!=SM_AUDIO_STATUS || r->length!=sizeof(id))return SM_INVALID;
    }
    if(result==SM_OK || result==SM_BUSY) {
        portENTER_CRITICAL(&lock);
        SmAudioStatus status={session,RING_FRAMES-(wr-rd),position,completed,underruns,wr,playing};
        portEXIT_CRITICAL(&lock);
        memcpy(reply->payload,&status,sizeof(status));reply->length=sizeof(status);
    }
    return result;
}
#endif

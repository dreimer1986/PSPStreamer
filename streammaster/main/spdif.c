/* SPDX-License-Identifier: GPL-2.0-or-later
 * IEC60958 transmitter. I2S is only a bit serializer: no DAC or I2S receiver
 * is connected to this pin. DMA completion, not USB acceptance, is the clock.
 */
#include "spdif.h"
#include "board.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include <stdlib.h>

#if SM_GENERIC_BOARD
int sm_audio_command(const SmFrame *request,SmFrame *reply) {
    (void)request;(void)reply;return SM_INVALID;
}
void sm_audio_disconnect(void) {}
#else
#define RING_FRAMES 8192U
#define BLOCK_FRAMES 192U
#define DMA_BLOCKS 6U
typedef struct {uint32_t stereo,end_pts_ms;} Sample;
typedef struct {void *buffer;uint32_t count,end_pts;} Completion;
static i2s_chan_handle_t output;
static Sample *ring;
static Completion dma[DMA_BLOCKS];
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static uint32_t rd,wr,rate,non_audio,session,sequence,completed,position,underruns;
static unsigned frame,level,paused,disconnected,playing;
static uint16_t bmc[256];

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
static void close_output(void) {
    if(output){i2s_channel_disable(output);i2s_del_channel(output);output=NULL;}
#if !SM_GENERIC_BOARD
    gpio_set_direction(GPIO_NUM_12,GPIO_MODE_OUTPUT);gpio_set_level(GPIO_NUM_12,0);
#endif
    free(ring);ring=NULL;
    memset(dma,0,sizeof(dma));
}
static int open_output(const SmAudioOpen *request) {
    if((request->rate!=32000 && request->rate!=44100 && request->rate!=48000)||request->non_audio>1)return SM_INVALID;
    close_output();
    ring=heap_caps_malloc(sizeof(*ring)*RING_FRAMES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!ring)return SM_IO;
    for(unsigned i=0;i<256;i++)bmc[i]=encode_byte(i);
    rate=request->rate;non_audio=request->non_audio;
    rd=wr=sequence=completed=position=underruns=frame=level=playing=disconnected=0;
    paused=1;session++;if(!session)session++;
    i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO,I2S_ROLE_MASTER);
    channel.dma_desc_num=DMA_BLOCKS;channel.dma_frame_num=BLOCK_FRAMES*2;
    if(i2s_new_channel(&channel,&output,NULL)!=ESP_OK)goto fail;
    i2s_std_config_t config={
        .clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(rate*2),
        .slot_cfg=I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,I2S_SLOT_MODE_STEREO),
        .gpio_cfg={.mclk=I2S_GPIO_UNUSED,.bclk=I2S_GPIO_UNUSED,.ws=I2S_GPIO_UNUSED,
                   .dout=GPIO_NUM_12,.din=I2S_GPIO_UNUSED}
    };
    if(i2s_channel_init_std_mode(output,&config)!=ESP_OK)goto fail;
    i2s_event_callbacks_t callbacks={.on_sent=sent};
    if(i2s_channel_register_event_callback(output,&callbacks,NULL)!=ESP_OK)goto fail;
    /* Preload valid silence instead of initially emitting unframed zero bits. */
    uint32_t *silence=malloc(BLOCK_FRAMES*16);
    if(!silence)goto fail;
    for(unsigned i=0;i<BLOCK_FRAMES;i++) {
        subframe(silence+i*4,0,0);subframe(silence+i*4+2,0,1);frame=(frame+1)%192;
    }
    esp_err_t err=ESP_OK;
    for(unsigned i=0;i<DMA_BLOCKS;i++) {
        size_t loaded=0;err=i2s_channel_preload_data(output,silence,BLOCK_FRAMES*16,&loaded);
        if(err!=ESP_OK || loaded!=BLOCK_FRAMES*16){err=ESP_FAIL;break;}
    }
    free(silence);
    if(err!=ESP_OK || i2s_channel_enable(output)!=ESP_OK)goto fail;
    return SM_OK;
fail:
    close_output();return SM_IO;
}
int sm_audio_command(const SmFrame *r,SmFrame *reply) {
    int result=SM_OK;
    if(r->op==SM_AUDIO_OPEN) {
        SmAudioOpen request;
        if(r->length!=sizeof(request))return SM_INVALID;
        memcpy(&request,r->payload,sizeof(request));result=open_output(&request);
    } else {
        uint32_t id;
        if(r->length<sizeof(id))return SM_INVALID;
        memcpy(&id,r->payload,sizeof(id));
        if(!output || id!=session || disconnected)return SM_OFFLINE;
        if(r->op==SM_AUDIO_CLOSE){close_output();return SM_OK;}
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
            else {
                for(unsigned i=0;i<request.frames;i++) {
                    Sample *s=&ring[(wr+i)%RING_FRAMES];
                    memcpy(&s->stereo,r->payload+sizeof(request)+i*4,4);
                    s->end_pts_ms=request.pts_ms+(uint64_t)(i+1)*1000U/rate;
                }
                wr+=request.frames;sequence++;
            }
            portEXIT_CRITICAL(&lock);
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

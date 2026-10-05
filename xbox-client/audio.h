/* GPL-2.0-or-later. Sample-position clock from the nxdk AC97 DMA descriptors.
 * No callback-count clock and no guessed SDL/hardware queue latency. */
#include <hal/audio.h>
#include "spectrum_analysis.h"
#include "audio_dma_cursor.h"
extern AC97_DEVICE ac97Device;
static unsigned char *pcm;
static int volume=100;static unsigned audio_peaks[32][2];
/* Cached normal-RAM copies of the DMA-current waveform. Do not read back
 * write-combined DMA memory and do not transform audio ahead of playback. */
static short audio_waveform[32][2304];
static int audio_analysis;
static int64_t audio_pts[32];static unsigned audio_serial[32],audio_sent;
static int audio_running,audio_failed,audio_initialized;static int64_t audio_clock;
static unsigned audio_tail;static int64_t audio_end_pts;static int audio_dma_ran[2];
static unsigned audio_completed[2];
static volatile unsigned char *const ac97=(volatile unsigned char *)0xfec00000;

static int audio_reset(void){
    XAudioPause();
    ac97[0x11b]=0x1e;ac97[0x17b]=0x1e;
    Uint32 start=SDL_GetTicks();
    while((ac97[0x11b]&2)||(ac97[0x17b]&2)){if(SDL_GetTicks()-start>100)return 0;SDL_Delay(1);}
    ac97[0x116]=0xff;ac97[0x176]=0xff;
    *(volatile unsigned *)(ac97+0x110)=MmGetPhysicalAddress((void*)ac97Device.pcmOutDescriptor);
    *(volatile unsigned *)(ac97+0x170)=MmGetPhysicalAddress((void*)ac97Device.pcmSpdifDescriptor);
    ac97Device.nextDescriptor=0;
    memset((void*)ac97Device.pcmOutDescriptor,0,sizeof(ac97Device.pcmOutDescriptor));
    memset((void*)ac97Device.pcmSpdifDescriptor,0,sizeof(ac97Device.pcmSpdifDescriptor));
    memset(audio_serial,0,sizeof(audio_serial));memset(audio_peaks,0,sizeof(audio_peaks));audio_sent=0;audio_clock=-1;audio_running=0;
    memset(audio_waveform,0,sizeof(audio_waveform));spectrum_analysis_reset();
    audio_tail=0;audio_end_pts=0;memset(audio_dma_ran,0,sizeof(audio_dma_ran));
    memset(audio_completed,0,sizeof(audio_completed));
    return 1;
}
static int audio_init(void){
    if(audio_initialized)return !audio_failed;
    XAudioInit(16,2,NULL,NULL);audio_initialized=1;
    pcm=MmAllocateContiguousMemoryEx(32*4608,0,0xffffffff,0,PAGE_READWRITE|PAGE_WRITECOMBINE);
    if(!pcm||!audio_reset())audio_failed=1;
    return !audio_failed;
}
static unsigned audio_cursor(unsigned base,int64_t *pts){
    unsigned first=0,second=0,remaining=0,status=0,last=0,control=0;
    for(int tries=0;tries<8;tries++){
        first=ac97[base+4]&31;
        remaining=*(volatile unsigned short*)(ac97+base+8);
        status=ac97[base+6];last=ac97[base+5]&31;control=ac97[base+11];
        second=ac97[base+4]&31;
        if(first==second)break;
    }
    unsigned serial=audio_serial[second];
    if(first!=second){*pts=-1;return 0;}
    int channel=base==0x170;
    if((control&1)&&(!(status&1)||(remaining>0&&remaining<2304)))audio_dma_ran[channel]=1;
    if(xbox_dma_tail_reached(serial,audio_serial[last],audio_tail,status,control,audio_dma_ran[channel])){
        *pts=audio_end_pts;return audio_tail;
    }
    if(!serial||remaining>2304){*pts=-1;return 0;}
    remaining=xbox_dma_remaining(remaining,status,control,second,last);
    *pts=audio_pts[second]+(int64_t)(2304-remaining)*90000/96000;
    return serial-(remaining?1:0);
}
static unsigned audio_queued(void){
    if(!audio_running)return audio_sent;
    int64_t analog,digital;unsigned a=audio_cursor(0x110,&analog),d=audio_cursor(0x170,&digital);
    /* CIV may expose an older ring slot at underflow/prefetch. Completed
     * descriptors and the sample clock cannot go backwards within a stream. */
    a=xbox_dma_completed(a,&audio_completed[0]);
    d=xbox_dma_completed(d,&audio_completed[1]);
    if(analog>=0&&analog>audio_clock)audio_clock=analog;
    unsigned completed=a<d?a:d;
    return completed<=audio_sent?audio_sent-completed:0;
}
static void audio_submit(plm_samples_t *samples,int64_t pts){
    unsigned index=ac97Device.nextDescriptor;
    int16_t *out=(int16_t*)(pcm+index*4608);
    audio_peaks[index][0]=audio_peaks[index][1]=0;
    for(int i=0;i<2304;i++){float f=samples->interleaved[i]*(32767.0f*volume/100.0f);if(f>32767)f=32767;if(f<-32768)f=-32768;out[i]=(int16_t)f;
        unsigned peak=out[i]<0?-out[i]:out[i];if(peak>audio_peaks[index][i&1])audio_peaks[index][i&1]=peak;}
    audio_pts[index]=pts;audio_serial[index]=++audio_sent;
    if(audio_analysis)for(int i=0;i<2304;i++){
        float value=samples->interleaved[i]*(32767.f*volume/100.f);
        audio_waveform[index][i]=(short)fminf(32767,fmaxf(-32768,value));
    }
    /* Drain write-combining stores before publishing the DMA descriptor. */
    __asm__ volatile("sfence" ::: "memory");
    XAudioProvideSamples((unsigned char*)out,4608,FALSE);
}
static void audio_submit_tail(void){
    if(audio_tail||!audio_sent)return;
    unsigned index=ac97Device.nextDescriptor,previous=(index+31)&31;
    int16_t *out=(int16_t*)(pcm+index*4608);
    memset(out,0,4608);memset(audio_waveform[index],0,sizeof(audio_waveform[index]));
    audio_peaks[index][0]=audio_peaks[index][1]=0;
    audio_end_pts=audio_pts[previous]+2160; /* 1152 real samples at 48 kHz, 90 kHz PTS */
    audio_pts[index]=audio_end_pts;audio_serial[index]=++audio_sent;audio_tail=audio_sent;
    __asm__ volatile("sfence" ::: "memory");
    XAudioProvideSamples((unsigned char*)out,4608,TRUE);
}

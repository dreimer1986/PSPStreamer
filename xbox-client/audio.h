/* GPL-2.0-or-later. Sample-position clock from the nxdk AC97 DMA descriptors.
 * No callback-count clock and no guessed SDL/hardware queue latency. */
#include <hal/audio.h>
extern AC97_DEVICE ac97Device;
static unsigned char *pcm;
static int volume=100;static unsigned audio_peaks[32][2];
static float audio_spectrum[32][24];
static int audio_analysis;
static int64_t audio_pts[32];static unsigned audio_serial[32],audio_sent;
static int audio_running,audio_failed,audio_initialized;static int64_t audio_clock;
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
    memset(audio_spectrum,0,sizeof(audio_spectrum));
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
    unsigned first=0,second=0,remaining=0;
    for(int tries=0;tries<8;tries++){
        first=ac97[base+4]&31;
        remaining=*(volatile unsigned short*)(ac97+base+8);
        second=ac97[base+4]&31;
        if(first==second)break;
    }
    unsigned serial=audio_serial[second];
    if(!serial||first!=second||remaining>2304){*pts=-1;return 0;}
    *pts=audio_pts[second]+(int64_t)(2304-remaining)*90000/96000;
    return serial-(remaining?1:0);
}
static unsigned audio_queued(void){
    if(!audio_running)return audio_sent;
    int64_t analog,digital;unsigned a=audio_cursor(0x110,&analog),d=audio_cursor(0x170,&digital);
    if(analog>=0)audio_clock=analog;
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
    /* Small real frequency analysis, once per decoded audio block; display
     * reads the DMA-current block rather than future decoded samples. */
    static float coefficient[24],window[256];static int spectrum_ready;
    if(audio_analysis&&!spectrum_ready){float hz=90;
        for(int b=0;b<24;b++){coefficient[b]=2*cosf(6.2831853f*hz/48000);hz*=1.235f;}
        for(int i=0;i<256;i++)window[i]=.5f-.5f*cosf(6.2831853f*i/255);
        spectrum_ready=1;
    }
    if(audio_analysis)for(int b=0;b<24;b++){float s1=0,s2=0,c=coefficient[b];
        for(int i=0;i<256;i++){float s=((float)out[i*2]+out[i*2+1])*(window[i]/65536)+c*s1-s2;s2=s1;s1=s;}
        float power=s1*s1+s2*s2-c*s1*s2;float level=power>0?sqrtf(power)/32:0;
        audio_spectrum[index][b]=level>1?1:level;
    }
    /* Drain write-combining stores before publishing the DMA descriptor. */
    __asm__ volatile("sfence" ::: "memory");
    XAudioProvideSamples((unsigned char*)out,4608,FALSE);
}

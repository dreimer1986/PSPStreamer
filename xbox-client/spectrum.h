/* PSP analyzer and pixel painter reused unchanged. Xbox only supplies current
 * PCM, a clock and an SDL texture. No changes to the PSP compilation unit. */
#include "spectrum_paint.h"
static SDL_Texture *spectrum_texture;
static uint32_t spectrum_pixels[660*385];
static SpectrumPaint spectrum_painter;
static int spectrum_shown[64];
static void spectrum_clear(void *ctx,int x,int y,int w,int h){
    (void)ctx;for(int row=y;row<y+h;row++)memset(spectrum_pixels+row*660+x,0,w*4);
}
/* Same integer 12-bin equations as PSP main.c:spectrum_measure. */
static int legacy_level(const short *samples,int band){
    static const short coeff[]={8151,8035,7839,7568,7225,6811,5793,4551,2381,0,-3134,-6332};
    long long a=0,b=0;
    for(int i=0;i<64;i++){long long c=((samples[2*i]+samples[2*i+1])>>7)+((long long)coeff[band]*a>>12)-b;b=a;a=c;}
    unsigned long long value=a*a+b*b-((long long)coeff[band]*a*b>>12),bit=1ULL<<62,root=0;
    while(bit>value)bit>>=2;while(bit){if(value>=root+bit){value-=root+bit;root=(root>>1)+bit;}else root>>=1;bit>>=2;}
    int level=(root>32767?32767:(int)root)/30;return level>100?100:level;
}
static void spectrum_draw(int full){
    unsigned idx=ac97[0x114]&31;unsigned long long now=(unsigned long long)SDL_GetTicks()*1000;
    int active=audio_running&&!paused&&!underrun;
    spectrum_analysis_output(0);
    if(active)spectrum_pcm_publish(audio_waveform[idx],576);
    spectrum_analysis_step(now,1,active);
    int x=full?30:37,y=full?40:147,w=full?660:488,h=full?385:135;
    static int old_h,old_count,old_style=-1;
    int count=spectrum_bar_count(),style=spectrum_style_key();
    if(h!=old_h||count!=old_count||style!=old_style){memset(&spectrum_painter,0,sizeof(spectrum_painter));memset(spectrum_shown,0,sizeof(spectrum_shown));old_h=h;old_count=count;old_style=style;}
    memset(spectrum_pixels,0,sizeof(spectrum_pixels));
    for(int i=0;i<count;i++){
        int target=active?spectrum_bar_level(i,spectrum_analysis_mode?0:legacy_level(audio_waveform[idx],i)):0;
        int prev=spectrum_shown[i];spectrum_shown[i]=target>prev?prev+(target-prev+1)/2:prev>3?prev-3:0;
        int dirty;spectrum_paint_bar(&spectrum_painter,spectrum_pixels,660,i*w/count,h,w/count-2,h,i,count,spectrum_shown[i]*h/100,now,1,spectrum_clear,NULL,&dirty);
    }
    if(!spectrum_texture)spectrum_texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ABGR8888,SDL_TEXTUREACCESS_STREAMING,660,385);
    if(spectrum_texture){SDL_UpdateTexture(spectrum_texture,NULL,spectrum_pixels,660*4);SDL_Rect src={0,0,w,h},dst={x,y,w,h};SDL_RenderCopy(renderer,spectrum_texture,&src,&dst);}
}

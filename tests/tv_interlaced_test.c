#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../psp-client/tv_output_layout.h"
#include "../psp-fusa-probe/scale.h"
#include "../psp-overclock/overlay_pixels.h"
int main(void)
{
    assert(!tv_model_has_output(0)&&!tv_model_has_output(10));
    assert(tv_model_has_output(1)&&tv_model_has_output(2)&&tv_model_has_output(4));
    assert(!tv_cable_supported(-1)&&!tv_cable_supported(0)&&tv_cable_supported(1)&&tv_cable_supported(2));
    assert(tv_choose_interlaced(1,TV_SCAN_PROGRESSIVE,0));
    assert(!tv_choose_interlaced(2,TV_SCAN_AUTO,0));
    assert(tv_choose_interlaced(2,TV_SCAN_AUTO,0x2d1));
    assert(tv_choose_interlaced(2,TV_SCAN_INTERLACED,0x1d2));
    assert(!tv_choose_interlaced(2,TV_SCAN_PROGRESSIVE,0x1d1));
    const unsigned pixels=768*TV_FIELD_ROWS;
    uint32_t *input=calloc(768*480,4);
    uint16_t *out=malloc((pixels+32)*2);
    uint16_t *game=malloc(480*272*2),*linear=calloc(768*480,2);
    assert(input&&out&&game&&linear);
    for(unsigned i=0;i<pixels+32;i++)out[i]=0x1234;
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<720;x++)input[y*768+x]=(y*353+x*27)&0xffffff;
    tv_pack_rect(out,input,768,0,0,720,480,1);
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<720;x++)
        assert(out[tv_scan_row(y,1)*768+x]==tv_pixel_565(input[y*768+x]));
    for(unsigned y=240;y<262;y++)for(unsigned x=0;x<768;x++)assert(out[y*768+x]==0x1234);
    for(unsigned i=pixels;i<pixels+32;i++)assert(out[i]==0x1234);
    assert(tv_scan_row(0,1)==262&&tv_scan_row(1,1)==0&&tv_scan_row(478,1)==501&&tv_scan_row(479,1)==239);
    /* Dirty rows must update both fields without moving neighbouring pixels. */
    input[8*768+8]=0xffffff;input[9*768+8]=0;
    tv_pack_rect(out,input,768,8,8,1,2,1);
    assert(out[tv_scan_row(8,1)*768+8]==0xffff&&out[tv_scan_row(9,1)*768+8]==0);
    for(unsigned i=0;i<480*272;i++)game[i]=(uint16_t)i;
    fs_scale16(linear,game,480);fs_scale16_interlaced(out,game,480);
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<720;x++)
        assert(linear[y*768+x]==out[tv_scan_row(y,1)*768+x]);
    assert(2*FS_FIELD_OUTPUT_BYTES<=0x200000);
    assert(oc_osd_layout(0x44000000,0x200000,720,503,768,0));
    assert(!oc_osd_layout(0x44180000,0x200000,720,503,768,0));
    assert(!oc_osd_layout(0x44000000,0x200000,720,504,768,0));
    const char text[3][40]={"INTERLACED","BOTH FIELDS","RESTORE"};
    uint16_t *saved=malloc(pixels*2);assert(saved);memcpy(saved,out,pixels*2);
    OcOverlay *overlay=calloc(1,sizeof(*overlay));assert(overlay);
    oc_osd_draw_layout(overlay,out,768,0,text,1);
    assert(memcmp(saved,out,pixels*2));
    oc_osd_restore(overlay);assert(!memcmp(saved,out,pixels*2));
    oc_osd_draw_fresh_layout(out,768,0,500,212,text,1);
    assert(out[tv_scan_row(8,1)*768+500]==oc_osd_color(0,0));
    assert(out[tv_scan_row(9,1)*768+500]==oc_osd_color(0,0));
    free(overlay);free(saved);free(input);free(out);free(game);free(linear);
    return 0;
}

#include <assert.h>
enum {PSP_CTRL_SQUARE=1,MD_FILE_OK=0};
static struct {unsigned int Buttons;} pad;
static unsigned int old;
static int visual_preset,music_visual_active,music_saved_visual_preset;
static int preset_result,music_preset_seconds=60,allow_start=1,starts,stops,begins;
static unsigned long long next_preset_tick,preset_notice_tick;
static int md_start(void) {starts++;return allow_start;}
static void md_stop(void) {stops++;}
static void md_begin_preset(unsigned int fade) {assert(!fade);begins++;}
static unsigned long long sceKernelGetSystemTimeWide(void) {return 1000;}
static void lcd_music_reset(void) {}
static void tv_music_reset(void) {}
static void input(unsigned int buttons) {
    pad.Buttons=buttons;
    /* SQUARE_BLOCK */
    old=buttons;
}
static void press(void) {input(0);input(PSP_CTRL_SQUARE);}
int main(void) {
    for(int i=0;i<9;i++) {
        press();
        int expected=i%3==0?4:i%3==1?6:0;
        assert(visual_preset==expected);
        assert(music_saved_visual_preset==visual_preset);
        assert(music_visual_active==(expected!=0));
        input(PSP_CTRL_SQUARE); /* Holding does not toggle again. */
        assert(visual_preset==expected);
    }
    assert(starts==6 && stops==9 && begins==3);
    assert(next_preset_tick==60001000 && preset_notice_tick==~0ULL);
    preset_result=1;press();
    assert(visual_preset==4 && !music_visual_active && starts==6);
    press();assert(visual_preset==6 && music_visual_active);
    press();assert(!visual_preset);
    preset_result=0;allow_start=0;press();
    assert(!visual_preset && !music_visual_active && !music_saved_visual_preset);
    allow_start=1;press();assert(visual_preset==4 && music_visual_active);
    return 0;
}

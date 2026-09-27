/* UI harnesses drive known levels; FFT math is tested separately. */
#define SPECTRUM_MAX_BANDS 64
int spectrum_style,spectrum_segments,spectrum_peak_hold;
static int test_spectrum_count=12;
static int spectrum_bar_count(void){return test_spectrum_count;}
static int spectrum_bar_level(int i,int legacy){(void)i;return legacy;}

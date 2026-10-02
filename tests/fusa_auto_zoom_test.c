#include <assert.h>
#include "../psp-fusa-probe/auto_zoom.h"
int main(void)
{
    int enabled=0,keep=0;unsigned seconds=5;
    char a[]=" auto_zoom = 1 ; on",b[]="auto_zoom_delay_seconds=12\r";
    char c[]="auto_zoom=2",d[]="auto_zoom_delay_seconds=0",e[]="auto_zoom_delay_seconds=9999999999999";
    char f[]="auto_zoom=0garbage",g[]=";auto_zoom=0",h[]="auto_zoom_delay_seconds=60";
    fs_auto_option(a,&enabled,&seconds,&keep);assert(enabled==1);
    fs_auto_option(b,&enabled,&seconds,&keep);assert(seconds==12);
    fs_auto_option(c,&enabled,&seconds,&keep);fs_auto_option(d,&enabled,&seconds,&keep);
    fs_auto_option(e,&enabled,&seconds,&keep);fs_auto_option(f,&enabled,&seconds,&keep);
    fs_auto_option(g,&enabled,&seconds,&keep);assert(enabled==1&&seconds==12);
    fs_auto_option(h,&enabled,&seconds,&keep);assert(seconds==60);
    char k[]="keep_fullscreen=1",l[]="keep_fullscreen=2",m[]="keep_fullscreen=0";
    fs_auto_option(k,&enabled,&seconds,&keep);assert(keep==1);
    fs_auto_option(l,&enabled,&seconds,&keep);assert(keep==1);
    assert(!fs_button_exit(keep,0x10000,0x10000));
    assert(fs_zoom_wanted(0,keep,1));assert(!fs_zoom_wanted(0,keep,0));
    fs_auto_option(m,&enabled,&seconds,&keep);assert(keep==0);
    assert(fs_button_exit(keep,0x10000,0x10000));
    assert(!fs_zoom_wanted(0,keep,1));assert(fs_zoom_wanted(1,keep,0));
    FsAutoZoom state={0};
    assert(!fs_auto_tick(&state,0,1,5));
    assert(!fs_auto_tick(&state,4999999,1,5));
    assert(fs_auto_tick(&state,5000000,1,5));
    assert(!fs_auto_tick(&state,20000000,1,5)); /* No retry after off/error. */
    assert(!fs_auto_tick(&state,21000000,0,5)); /* LCD rearms. */
    assert(!fs_auto_tick(&state,22000000,1,5));
    assert(fs_auto_tick(&state,27000000,1,5));
    state=(FsAutoZoom){0};state.handled=1; /* Manual override. */
    assert(!fs_auto_tick(&state,40000000,1,5));
    return 0;
}

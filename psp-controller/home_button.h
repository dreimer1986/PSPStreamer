/* SPDX-License-Identifier: GPL-2.0-or-later
 * Start+Select emits one bounded system-only HOME pulse per hold. */
typedef struct { unsigned long long since; int holding,fired; } PadHome;
static unsigned pad_home_button(PadHome *state,unsigned *buttons,
                                unsigned long long now,int enabled) {
    const unsigned chord=PSP_CTRL_START|PSP_CTRL_SELECT;
    if(!enabled || (*buttons&chord)!=chord){
        state->holding=state->fired=0;return 0;
    }
    *buttons&=~chord;
    if(!state->holding){state->holding=1;state->since=now;}
    if(!state->fired && now-state->since>=1000000){
        state->fired=1;state->since=now;
    }
    return state->fired && now-state->since<150000?PSP_CTRL_HOME:0;
}

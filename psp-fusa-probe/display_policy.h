/* SPDX-License-Identifier: MIT */
#ifndef FS_DISPLAY_POLICY_H
#define FS_DISPLAY_POLICY_H
#include <stdint.h>
/* Relocate only ordinary, position-independent prologue instructions. Never
 * copy a branch, jump, privileged operation or another plugin's entry hook. */
static int fs_relocatable(uint32_t insn)
{
    unsigned op=insn>>26,fn=insn&63;
    if(!op)return fn==0||fn==2||fn==3||fn==4||fn==6||fn==7||
        fn==0x21||fn==0x23||fn==0x24||fn==0x25||fn==0x26||fn==0x27||fn==0x2a||fn==0x2b;
    return (op>=8&&op<=15)||(op>=0x20&&op<=0x26)||op==0x28||op==0x29||op==0x2b;
}
static int fs_game_tv_layout(int mode,int width,int height)
{return (mode==0x2d2||mode==0x1d2)&&width==480&&height==272;}
static int fs_redirect_mode(int active,int own,int lcd_requested,int mode,int width,int height)
{return active&&!own&&!lcd_requested&&(mode==0||mode==0x1d2||mode==0x2d2)&&width==480&&height==272;}
static int fs_observation_valid(unsigned before,unsigned after,unsigned writers_before,unsigned writers_after,int idle)
{return before==after&&!writers_before&&!writers_after&&idle;}
enum { FS_FORWARD,FS_SYSTEM,FS_GAME };
static int fs_layer_route(int active,int own,int layer)
{return !active||own?FS_FORWARD:layer==0?FS_SYSTEM:layer==2?FS_GAME:FS_FORWARD;}
/* Sony's public game buffer is internal selector 2, not selector 1. */
static int fs_display_layer(unsigned index){return index?2:0;}
#endif

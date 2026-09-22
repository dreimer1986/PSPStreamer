/* SPDX-License-Identifier: GPL-2.0-or-later
 * Original bounded tunnel prototype; no Monkey code or artwork. */
#ifndef PSPSTREAMER_TUNNEL_VISUAL_H
#define PSPSTREAMER_TUNNEL_VISUAL_H
#include "milkdrop_warp.h"
enum { TUNNEL_SIDES=24, TUNNEL_RINGS=28, TUNNEL_TEXTURE=64,
       TUNNEL_VERTICES=(TUNNEL_RINGS-1)*TUNNEL_SIDES*6 };
typedef struct {
    float travel, bass, level;
    unsigned long long previous;
} TunnelState;
void tunnel_texture(uint32_t *pixels);
int tunnel_mesh(TunnelState *state,MdVertex *out,int capacity,
                const unsigned char bands[12],int level,unsigned long long now);
#endif

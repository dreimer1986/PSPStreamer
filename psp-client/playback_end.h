/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
/* A transport failure is never a natural media end, even within the duration
 * tolerance. Require progress in this attempt so a failed reconnect near EOF
 * cannot advance the playlist merely because its resume offset is large. */
static inline int playback_end_allowed(int clean_end,int error,int progress,
                                       int position_ms,float duration_seconds) {
    if(!clean_end || error || progress<=0)return 0;
    return duration_seconds<=0.0f ||
        (double)position_ms >= (double)duration_seconds*1000.0-2000.0;
}

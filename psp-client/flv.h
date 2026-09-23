/* SPDX-License-Identifier: GPL-2.0-or-later
 * A/V scheduling adapted from PMPlayer Advance, Copyright (C) 2010 cooleyes.
 * Streaming adaptation: 2026-09-07, PSP Streamer. See ../NOTICE and ../LICENSE.
 * Streaming FLV framing for the FFmpeg H.264/MP3 output contract.
 * PPA reference: ppa/mod/flv1_read.c and flv1_play.c (cooleyes).
 * PTS = tag DTS + signed AVC composition offset. No frame-count clock.
 */
#ifndef PSP_STREAMER_FLV_H
#define PSP_STREAMER_FLV_H
#include <stdint.h>
#include <string.h>
#include "avcc_packet.h"
#define FLV_MAX_VIDEO (256 * 1024)
#define FLV_MAX_AUDIO 2048
typedef AvcConfig FlvAvc;
static unsigned int flv_u24(const unsigned char *p) {
    return ((unsigned int)p[0] << 16) | ((unsigned int)p[1] << 8) | p[2];
}
static unsigned int flv_u32(const unsigned char *p) {
    return (flv_u24(p) << 8) | p[3];
}
static int flv_pts(const unsigned char *tag, const unsigned char *body) {
    int cts = (int)flv_u24(body + 2);
    if (cts & 0x800000) cts -= 0x1000000;
    return (int)(flv_u24(tag + 4) | ((unsigned int)tag[7] << 24)) + cts;
}
/* Parse a configuration snapshot; queued packets own their own copy. */
static int flv_config(FlvAvc *s, const unsigned char *p, int n) {
    return avcc_config_parse(s,p,n);
}
/* PPA's avsync_status, in container milliseconds. */
static int pts_avsync(int audio, int video, int duration) {
    int64_t delta = (int64_t)video - audio;
    if (delta > 2LL * duration) return 0;
    if (-delta > 2LL * duration) return 2;
    return 1;
}
/* Only a prepared picture may release the startup barrier. Once the first
 * picture has released the DAC, wait for its clock before showing more. */
static int pts_presentation_status(int prepared, int first_presented,
                                  int audio_active, int clock_started,
                                  int audio, int video, int duration, int wall_pts) {
    if (!prepared) return 0;
    if (audio_active) {
        if (!clock_started) return first_presented ? 0 : 1;
        return pts_avsync(audio, video, duration);
    }
    return wall_pts < video ? 0 : 1;
}
#endif

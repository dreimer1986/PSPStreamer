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
#define FLV_MAX_VIDEO (256 * 1024)
#define FLV_MAX_AUDIO 2048
typedef struct {
    unsigned char headers[1024];
    int header_size, length_size;
} FlvAvc;
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
/* Convert AVCDecoderConfigurationRecord into Annex-B SPS/PPS. */
static int flv_config(FlvAvc *s, const unsigned char *p, int n) {
    int at = 6, group, count, i;
    if (n < 7 || p[0] != 1) return -1;
    s->length_size = (p[4] & 3) + 1;
    s->header_size = 0;
    count = p[5] & 31;
    for (group = 0; group < 2; group++) {
        for (i = 0; i < count; i++) {
            int size;
            if (at + 2 > n) return -1;
            size = (p[at] << 8) | p[at + 1]; at += 2;
            if (!size || at + size > n || s->header_size + size + 4 > 1024) return -1;
            memcpy(s->headers + s->header_size, "\0\0\0\1", 4);
            memcpy(s->headers + s->header_size + 4, p + at, size);
            s->header_size += size + 4; at += size;
        }
        if (!group) { if (at >= n) return -1; count = p[at++]; }
    }
    return s->header_size ? 0 : -1;
}
static int flv_annexb(const FlvAvc *s, const unsigned char *p, int n,
                      unsigned char *out, int capacity) {
    int at = 0, used = s->header_size;
    if (!used || !s->length_size || n <= 0 || used > capacity) return -1;
    memcpy(out, s->headers, used);
    while (at < n) {
        unsigned int size = 0;
        int i;
        if (at + s->length_size > n) return -1;
        for (i = 0; i < s->length_size; i++) size = (size << 8) | p[at++];
        if (!size || used + 4 > capacity || size > (unsigned int)(n - at) ||
            size > (unsigned int)(capacity - used - 4)) return -1;
        memcpy(out + used, "\0\0\0\1", 4);
        memcpy(out + used + 4, p + at, size);
        used += 4 + size; at += size;
    }
    return used;
}
/* PPA's avsync_status, in container milliseconds. */
static int pts_avsync(int audio, int video, int duration) {
    int64_t delta = (int64_t)video - audio;
    if (delta > 2LL * duration) return 0;
    if (-delta > 2LL * duration) return 2;
    return 1;
}
#endif

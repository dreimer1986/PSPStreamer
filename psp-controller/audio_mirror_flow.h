/* SPDX-License-Identifier: GPL-2.0-or-later */
#define MIRROR_RING 4096U
/* Only shed a small block near exhaustion; normal USB jitter must not discard
 * 46+ ms at once. The producer never overwrites unread frames. */
static unsigned mirror_trim_frames(unsigned available) {
    return available>3840U ? available-3584U : 0;
}
#define MIRROR_ESP_TARGET 3072U
#define MIRROR_PACKET_MAX 960U
static unsigned mirror_packet_frames(unsigned available,unsigned queued,unsigned space) {
    if(queued>=MIRROR_ESP_TARGET)return 0;
    unsigned frames=available;
    if(frames>MIRROR_PACKET_MAX)frames=MIRROR_PACKET_MAX;
    if(frames>space)frames=space;
    if(frames>MIRROR_ESP_TARGET-queued)frames=MIRROR_ESP_TARGET-queued;
    frames&=~63U;
    return frames>=(queued<1024U?256U:512U)?frames:0;
}

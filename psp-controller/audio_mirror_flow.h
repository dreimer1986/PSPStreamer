/* SPDX-License-Identifier: GPL-2.0-or-later */
#define MIRROR_RING 4096U
/* Low 32-bit microsecond clocks wrap; RPCs are bounded well below one wrap.
 * Completion order is not assumed: both requests are submitted asynchronously. */
static inline int mirror_rpc_timing(unsigned begin,unsigned send,unsigned receive,unsigned end,
                                    unsigned *reply,unsigned *wake) {
    unsigned wall=end-begin,tx=send-begin,rx=receive-begin;
    if(tx>wall || rx>wall)return 0;
    *reply=rx;*wake=wall-(tx>rx?tx:rx);return 1;
}
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

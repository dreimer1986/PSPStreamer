/* SPDX-License-Identifier: GPL-2.0-or-later */
#define MIRROR_RING 4096U
/* Live audio is not a file: after a service stall do not replay a full old
 * capture ring. Keep 23 ms at 44.1 kHz; the ESP holds its own playback reserve.
 * Consumer advances rd before copying, producer never overwrites unread data. */
static unsigned mirror_trim_frames(unsigned available) {
    return available>3072U ? available-1024U : 0;
}

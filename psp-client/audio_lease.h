/* SPDX-License-Identifier: GPL-2.0-or-later
 * PPA output-thread ownership: keep the most recently submitted PCM block
 * until the next successful submission, or until the DAC has drained.
 */
#ifndef PSP_STREAMER_AUDIO_LEASE_H
#define PSP_STREAMER_AUDIO_LEASE_H
typedef struct { int held; } AudioLease;
static int audio_lease_submit(AudioLease *lease, int slot) {
    int released = lease->held;
    lease->held = slot;
    return released;
}
static int audio_lease_drain(AudioLease *lease) {
    int released = lease->held;
    lease->held = -1;
    return released;
}
#endif

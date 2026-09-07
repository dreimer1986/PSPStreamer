"""Exercise the actual DAC worker against a deterministic asynchronous DAC.

The fake DAC retains the submitted pointer until the next blocking call or
drain. A producer immediately reuses every returned ring slot. This exposes
early reuse, duplicate submissions, EOF deadlocks and startup cancellation.
It does not emulate the PSP codec or establish real hardware latency.
"""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

SHIM = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "audio_lease.h"
typedef unsigned int SceSize;
#define AUDIO_BLOCK_SAMPLES 4608
#define AUDIO_QUEUE_BLOCKS 8
#define PSP_AUDIO_FORMAT_STEREO 0
#define PSP_AUDIO_VOLUME_MAX 32768
static short audio_samples[AUDIO_BLOCK_SAMPLES * 2 * AUDIO_QUEUE_BLOCKS];
static unsigned int audio_block_timestamp_ms[AUDIO_QUEUE_BLOCKS];
static unsigned int audio_current_timestamp_ms;
static int audio_dac_samples = AUDIO_BLOCK_SAMPLES, playback_volume = 30;
static int audio_running, audio_start, audio_queue_primed, timed_active;
static int video_first_presented, timed_running, audio_clock_started;
static int audio_queue_read, audio_state, sync_audio_channel, sync_audio_pts_errors;
static int audio_played_blocks, audio_blocks_published;
static int audio_queue_ready_sema = 2, audio_queue_free_sema = 3;
static int target, submitted, freed, active_slot, active_tag, cancel_start, fail_at;
static unsigned long long tick, drain_at;
static unsigned long long sceKernelGetSystemTimeWide(void) { return tick; }
static void verify_dma(void) {
    if (active_slot >= 0)
        assert(audio_samples[active_slot * AUDIO_BLOCK_SAMPLES * 2] == active_tag);
}
static int sceKernelDelayThread(int us) {
    tick += us;
    if (!video_first_presented) {
        if (cancel_start) audio_running = timed_running = 0;
        else video_first_presented = 1;
    }
    return 0;
}
static void publish(void) {
    int slot = audio_blocks_published % AUDIO_QUEUE_BLOCKS;
    assert(slot != active_slot);
    audio_samples[slot * AUDIO_BLOCK_SAMPLES * 2] = audio_blocks_published;
    audio_block_timestamp_ms[slot] = audio_blocks_published * 104;
    audio_blocks_published++;
}
static int sceKernelSignalSema(int sema, int count) {
    assert(sema == audio_queue_free_sema && count == 1);
    verify_dma();
    assert(freed % AUDIO_QUEUE_BLOCKS != active_slot);
    freed++;
    if (audio_blocks_published < target) publish();
    return 0;
}
static int audio_queue_wait(int sema) {
    assert(sema == audio_queue_ready_sema);
    if (submitted < audio_blocks_published) return 1;
    tick += 10000;
    if (submitted == target) audio_running = 0;
    return 0;
}
static int sceAudioGetChannelRestLen(int channel) {
    assert(channel == 0);
    verify_dma();
    if (active_slot >= 0 && tick >= drain_at) active_slot = -1;
    return active_slot < 0 ? 0 : AUDIO_BLOCK_SAMPLES;
}
static int sceAudioSRCChRelease(void) { return 0; }
static int sceAudioChReserve(int channel, int samples, int format) {
    assert(channel == 0 && samples == AUDIO_BLOCK_SAMPLES && format == 0);
    return channel;
}
static int sceAudioChRelease(int channel) {
    assert(channel == 0);
    verify_dma(); active_slot = -1;
    return 0;
}
static void sceKernelDcacheWritebackRange(void *data, int size) {
    (void)data; assert(size == AUDIO_BLOCK_SAMPLES * 4);
}
static int sceAudioOutputBlocking(int channel, int volume, void *data) {
    int slot = ((short *)data - audio_samples) / (AUDIO_BLOCK_SAMPLES * 2);
    assert(channel == 0 && volume == PSP_AUDIO_VOLUME_MAX);
    assert(video_first_presented);
    verify_dma();
    if (submitted == fail_at) return -1;
    assert(slot == submitted % AUDIO_QUEUE_BLOCKS);
    assert(*(short *)data == submitted); /* never repeat or skip content */
    assert(audio_current_timestamp_ms == (unsigned int)submitted * 104);
    active_slot = slot; active_tag = submitted++;
    tick += 104000; drain_at = tick + 104000;
    return AUDIO_BLOCK_SAMPLES;
}
'''

MAIN = r'''
int main(void) {
    int scenario;
    for (scenario = 0; scenario < 4; scenario++) {
        target = scenario == 0 ? 1 : 101;
        submitted = freed = audio_blocks_published = audio_played_blocks = 0;
        audio_queue_read = audio_state = audio_clock_started = sync_audio_pts_errors = 0;
        active_slot = -1; tick = 0;
        audio_running = audio_start = audio_queue_primed = timed_active = timed_running = 1;
        video_first_presented = 0;
        cancel_start = scenario == 2;
        fail_at = scenario == 3 ? 4 : -1;
        while (audio_blocks_published < target && audio_blocks_published < AUDIO_QUEUE_BLOCKS)
            publish();
        audio_output_thread(0, NULL);
        assert(active_slot == -1 && sync_audio_channel == -1);
        if (cancel_start) assert(submitted == 0);
        else if (fail_at >= 0) assert(audio_state == -20 && submitted == fail_at);
        else assert(submitted == target && freed == target && audio_played_blocks == target);
        assert(sync_audio_pts_errors == 0);
    }
    return 0;
}
'''


class AudioOutputTests(unittest.TestCase):
    def test_worker_ownership_startup_eof_and_failure(self):
        source = (ROOT / "psp-client/main.c").read_text()
        begin = source.index("static int audio_output_thread(")
        end = source.index("static int mp3_frame_size(", begin)
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            program = work / "output.c"
            program.write_text(SHIM + source[begin:end] + MAIN)
            binary = work / "output"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                            "-I", str(ROOT / "psp-client"), str(program), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)

/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>
#define SM_CAP_SPDIF 128U
enum {SM_AUDIO_OPEN=50,SM_AUDIO_WRITE,SM_AUDIO_STATUS,SM_AUDIO_PAUSE,SM_AUDIO_CLOSE};
/* Explicit opt-in. Only Onju V3 has a known/supported output pin (GPIO12).
 * Samples are little-endian stereo PCM words or IEC61937 carrier words.
 * Compressed carrier words must NEVER be attenuated or sent to a PCM DAC. */
typedef struct {uint32_t rate,non_audio;} SmAudioOpen;
typedef struct {uint32_t session,sequence,pts_ms,frames;} SmAudioWrite;
typedef struct {uint32_t session,paused;} SmAudioPause;
typedef struct {
    uint32_t session,space,position_ms,completed,underruns,accepted,playing;
} SmAudioStatus;
_Static_assert(sizeof(SmAudioWrite)==16,"SPDIF write ABI");
_Static_assert(sizeof(SmAudioStatus)==28,"SPDIF status ABI");

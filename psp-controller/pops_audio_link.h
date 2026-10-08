/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef POPS_AUDIO_LINK_H
#define POPS_AUDIO_LINK_H
#include "pops_audio_shared.h"
#define POPS_AUDIO_ABI 0x50414d01U
typedef struct {
    uint32_t abi;
    PopsAudioShared *shared;
    uint32_t published;
    int status;
} PopsAudioLink;
/* Kernel-only service: 0 query, 1 acquire, 2 release. */
typedef int (*PopsAudioService)(unsigned,PopsAudioLink *);
#endif

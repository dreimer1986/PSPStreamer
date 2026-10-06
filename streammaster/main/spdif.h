/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "../protocol.h"
#include "../spdif_protocol.h"
int sm_audio_command(const SmFrame *request,SmFrame *reply);
void sm_audio_disconnect(void);

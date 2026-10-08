/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "../protocol.h"
#include "../trace_protocol.h"
void sm_trace_event(unsigned kind,unsigned a,unsigned b);
void sm_trace_command(unsigned op,int result,unsigned duration);
void sm_trace_command_begin(unsigned op);
int sm_trace_command_read(const SmFrame *request,SmFrame *reply);
void sm_trace_audio(SmTrace *out);

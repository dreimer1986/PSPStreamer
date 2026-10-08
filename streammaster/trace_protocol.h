/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>
#define SM_DIAGNOSTICS 56U
#define SM_TRACE_EVENTS 16U
enum {SM_TRACE_USB_GONE=1,SM_TRACE_USB_RX,SM_TRACE_USB_TX,SM_TRACE_COMMAND_ERROR,
      SM_TRACE_AUDIO_OPEN,SM_TRACE_AUDIO_CLOSE,SM_TRACE_BT_STATE};
typedef struct {uint32_t ms,kind,a,b;} SmTraceEvent;
typedef struct {
    uint32_t version,uptime_ms,reset_reason,internal_free,internal_largest;
    uint32_t commands,last_op,last_command_ms,max_command_us,usb_events,event_count;
    uint32_t audio_session,audio_rate,audio_flags,audio_queued,audio_completed,audio_underruns;
    uint32_t dma_callbacks,max_dma_gap_us,bt_state;
    int32_t http_error;
    SmTraceEvent events[SM_TRACE_EVENTS];
} SmTrace;
_Static_assert(sizeof(SmTrace)==340,"diagnostic wire ABI");
/* Caller serializes writers. Pure helper shared with the bounded-ring test. */
static inline void sm_trace_record(SmTrace *t,uint32_t ms,uint32_t kind,uint32_t a,uint32_t b) {
    t->events[t->event_count%SM_TRACE_EVENTS]=(SmTraceEvent){ms,kind,a,b};
    t->event_count++;
    if(kind<=SM_TRACE_USB_TX)t->usb_events++;
    if(kind==SM_TRACE_BT_STATE)t->bt_state=a;
}

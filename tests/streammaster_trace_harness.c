/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "../streammaster/trace_protocol.h"
int main(void) {
    struct {unsigned before;SmTrace t;unsigned after;} s={.before=0x12345678,.after=0x87654321};
    for(unsigned i=0;i<10000;i++)sm_trace_record(&s.t,i*7,SM_TRACE_USB_RX,i,i+1);
    assert(s.before==0x12345678 && s.after==0x87654321);
    assert(s.t.event_count==10000 && s.t.usb_events==10000);
    for(unsigned i=9984;i<10000;i++) {
        SmTraceEvent *e=&s.t.events[i%SM_TRACE_EVENTS];
        assert(e->ms==i*7 && e->a==i && e->b==i+1 && e->kind==SM_TRACE_USB_RX);
    }
    sm_trace_record(&s.t,80000,SM_TRACE_BT_STATE,3,0);
    assert(s.t.bt_state==3 && s.t.usb_events==10000);
    assert(sizeof(s.t)<4096-32);
    puts("Diagnostic wire ABI and bounded event ring: OK");
}

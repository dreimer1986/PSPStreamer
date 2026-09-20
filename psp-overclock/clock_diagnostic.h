/* SPDX-License-Identifier: MIT
 * Worker-only RAM diagnostics. No I/O, allocation, locks or kernel calls here;
 * capture failures BEFORE releasing the clock guard. snapshot() writes later.
 */
#ifndef STREAMER_OC_CLOCK_DIAGNOSTIC_H
#define STREAMER_OC_CLOCK_DIAGNOSTIC_H
#include <stdio.h>
#include <string.h>
enum {
    OC_D_START=1,OC_D_SONY_GUARD,OC_D_SONY_CALL,OC_D_SONY_RETURN,
    OC_D_APPLY=10,OC_D_READY,OC_D_VALIDATE,OC_D_CHECKPOINT,
    OC_D_RATIO,OC_D_MULTIPLIER,OC_D_DOMAINS,OC_D_LOW_PROFILE,
    OC_D_RAMP,OC_D_RAMP_YIELD,OC_D_FINAL,
    OC_D_RESTORE=30,OC_D_RESTORE_RAMP,OC_D_RESTORE_DOMAINS
};
static unsigned int owned_ctl,owned_mul,owned_cpu,owned_bus;
typedef struct {
    int phase,line,result;unsigned int argument;
    unsigned int ctl,mul,cpu,bus,owned_ctl,owned_mul,owned_cpu,owned_bus;
    int active,run,suspend,target;
} OcClockRecord;
static struct {
    unsigned int guard_head,operation;
    OcClockRecord last,failure;
    unsigned int checkpoint[4];
    int failed;
    unsigned int guard_tail;
} oc_diagnostic={.guard_head=0x4f434442,.guard_tail=0x4244434f};
static void oc_diag_capture(OcClockRecord *r,int phase,int line,int result,unsigned int argument) {
    r->phase=phase;r->line=line;r->result=result;r->argument=argument;
    r->ctl=CTL;r->mul=MUL;r->cpu=CPU;r->bus=BUS;
    r->owned_ctl=owned_ctl;r->owned_mul=owned_mul;r->owned_cpu=owned_cpu;r->owned_bus=owned_bus;
    r->active=changed;r->run=running;r->suspend=suspended;r->target=target;
}
static void oc_diag_step(int phase,int line,unsigned int argument) {
    oc_diag_capture(&oc_diagnostic.last,phase,line,0,argument);
}
static int oc_diag_result(int result,int line) {
    oc_diag_capture(&oc_diagnostic.last,oc_diagnostic.last.phase,line,result,oc_diagnostic.last.argument);
    if(result<0 && !oc_diagnostic.failed) {
        oc_diagnostic.failure=oc_diagnostic.last;oc_diagnostic.failed=1;
    }
    return result;
}
static void oc_diag_begin(int phase,int line) {
    oc_diagnostic.operation++;oc_diagnostic.failed=0;
    memset(&oc_diagnostic.failure,0,sizeof(oc_diagnostic.failure));
    memset(oc_diagnostic.checkpoint,0,sizeof(oc_diagnostic.checkpoint));
    oc_diag_step(phase,line,0);
}
#define OC_STEP(phase,arg) oc_diag_step(phase,__LINE__,arg)
#define OC_RESULT(code) oc_diag_result((code),__LINE__)
static inline int oc_diag_format(char *out,size_t size) {
    const OcClockRecord *r=oc_diagnostic.failed?&oc_diagnostic.failure:&oc_diagnostic.last;
    return snprintf(out,size,
        "diag_version=1\ndiag_operation=%u\ndiag_guard=%08X/%08X\n"
        "diag_last_phase=%d\ndiag_last_line=%d\ndiag_last_result=%d\n"
        "diag_failed=%d\ndiag_phase=%d\ndiag_line=%d\ndiag_result=%d\ndiag_argument=%u\n"
        "diag_actual=%08X/%08X/%08X/%08X\n"
        "diag_owned=%08X/%08X/%08X/%08X\n"
        "diag_checkpoint_expected=%08X/%08X/%08X/%08X\n"
        "diag_changed=%d\ndiag_running=%d\ndiag_suspended=%d\ndiag_target=%d\n",
        oc_diagnostic.operation,oc_diagnostic.guard_head,oc_diagnostic.guard_tail,
        oc_diagnostic.last.phase,oc_diagnostic.last.line,oc_diagnostic.last.result,
        oc_diagnostic.failed,r->phase,r->line,r->result,r->argument,
        r->ctl,r->mul,r->cpu,r->bus,r->owned_ctl,r->owned_mul,r->owned_cpu,r->owned_bus,
        oc_diagnostic.checkpoint[0],oc_diagnostic.checkpoint[1],oc_diagnostic.checkpoint[2],oc_diagnostic.checkpoint[3],
        r->active,r->run,r->suspend,r->target);
}
#endif

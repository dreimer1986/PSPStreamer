/* MilkDrop permits exceptional intermediates, then sanitizes assignments.
 * PSP threads may enable IEEE traps by default, unlike typical host tests.
 * Scope non-trapping evaluation to the renderer; restore caller controls.
 * This does not change the rounding/flush mode or any other thread's state. */
#ifndef PSPSTREAMER_PRESET_FPU_H
#define PSPSTREAMER_PRESET_FPU_H
#ifdef __PSP__
#include <pspfpu.h>
typedef unsigned int MdFpuState;
static MdFpuState md_fpu_enter(void) {
    unsigned int previous, evaluation;
    __asm__ volatile("cfc1 %0, $31" : "=r"(previous));
    evaluation=previous&~(PSP_FPU_ENABLE_MASK|PSP_FPU_CAUSE_MASK|PSP_FPU_FLAGS_MASK);
    __asm__ volatile("ctc1 %0, $31\n\tnop\n\tnop" : : "r"(evaluation) : "memory");
    return previous;
}
static void md_fpu_leave(MdFpuState *previous) {
    /* Retire masked renderer exceptions BEFORE re-enabling caller traps.
     * Cause bits are transient, not policy: never replay a pending cause
     * when restoring enables. Keep original rounding, FS and sticky flags. */
    unsigned int clear=*previous&~(PSP_FPU_ENABLE_MASK|PSP_FPU_CAUSE_MASK|PSP_FPU_FLAGS_MASK);
    unsigned int restored=*previous&~PSP_FPU_CAUSE_MASK;
    __asm__ volatile(".set push\n\t.set noreorder\n\t"
        "ctc1 %0, $31\n\tnop\n\tnop\n\tnop\n\t"
        "ctc1 %1, $31\n\tnop\n\tnop\n\tnop\n\t.set pop"
        : : "r"(clear),"r"(restored) : "memory");
}
#else
#include <fenv.h>
typedef fenv_t MdFpuState;
static MdFpuState md_fpu_enter(void) {
    MdFpuState previous;
    feholdexcept(&previous);
    return previous;
}
static void md_fpu_leave(MdFpuState *previous) {fesetenv(previous);}
#endif
#endif

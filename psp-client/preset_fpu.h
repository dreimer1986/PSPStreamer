/* MilkDrop permits exceptional intermediates, then sanitizes assignments.
 * PSP threads may enable IEEE traps by default, unlike typical host tests.
 * Scope non-trapping evaluation to the renderer; restore the caller exactly.
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
    __asm__ volatile("ctc1 %0, $31\n\tnop\n\tnop" : : "r"(*previous) : "memory");
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

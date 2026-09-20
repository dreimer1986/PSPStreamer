/* SPDX-License-Identifier: MIT
 * Keep the reference tester's settling loop at the register-write site.
 * A plain static function was outlined by -O2, producing a jal immediately
 * after the PLL store. Do not depend on the compiler's inlining heuristic
 * during clock transitions. This avoids that call/return; it does NOT lock
 * instruction cache lines or prove that a PLL transition is hardware-safe.
 */
#ifndef STREAMEROC_CLOCK_SETTLE_H
#define STREAMEROC_CLOCK_SETTLE_H
static inline __attribute__((always_inline)) void settle(void) {
    __asm__ volatile(".set push\n.set noreorder\n.set noat\n.set nomacro\n"
        "sync\nlui $t0,0x02\nori $t0,$t0,0xffff\n"
        "1: nop\nnop\nnop\nnop\nnop\nnop\nnop\n"
        "addiu $t0,$t0,-1\nbnez $t0,1b\nnop\n.set pop\n"
        ::: "t0","memory");
}
#endif

/* SPDX-License-Identifier: MIT
 * Reference-style Sony baseline, once at startup, before direct PLL writes.
 * Never call Sony with interrupts/dispatch locked or during app profiles/exit.
 */
static int startup_apply(void) {
    if(!running || suspended)return -2;
    if(changed)return -3;
    if(target<66 || target>471)return -4;
    snapshot("clock_sony_baseline_begin");
    OcClockGuard g=oc_clock_lock();
    int result=ready();
    unsigned int index=CTL&15,num=(MUL>>8)&255,den=MUL&255;
    unsigned int cn=(CPU>>16)&511,cd=CPU&511,bn=(BUS>>16)&511,bd=BUS&511;
    /* Permit the observed inherited OC multiplier at ratio 3/4 for this
     * Sony call only. The direct writer's stricter validation stays intact. */
    int known=(num==9 && den==1) ||
        (den==OC_DEN && num>=OC_NORMAL_NUM && num<=oc_numerator(471));
    if(!result && (index<3 || index>5 || !cn || !bn || cn>cd || bn>bd || !known))result=-4;
    oc_clock_unlock(g);
    if(result)return result;
    if(!running || suspended)return -2;
    sony_baseline_result=scePowerSetClockFrequency(333,333,166);
    snapshot("clock_sony_baseline_result");
    if(!running || suspended)return -2;
    if(sony_baseline_result<0)return sony_baseline_result;
    /* ARK may return success without changing any registers. apply() reads
     * and validates the real state, and still refuses an unsafe mixed state. */
    return apply();
}

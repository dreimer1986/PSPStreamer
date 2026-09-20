/* SPDX-License-Identifier: MIT
 * Reference-style Sony baseline, once at startup, before direct PLL writes.
 * Never call Sony with interrupts/dispatch locked or during app profiles/exit.
 */
static int startup_apply(void) {
    oc_diag_begin(OC_D_START,__LINE__);
    if(!running || suspended)return OC_RESULT(-2);
    if(changed)return OC_RESULT(-3);
    if(target<66 || target>471)return OC_RESULT(-4);
    snapshot("clock_sony_baseline_begin");
    OcClockGuard g=oc_clock_lock();
    OC_STEP(OC_D_SONY_GUARD,0);
    int result=ready();
    unsigned int index=CTL&15,num=(MUL>>8)&255,den=MUL&255;
    unsigned int cn=(CPU>>16)&511,cd=CPU&511,bn=(BUS>>16)&511,bd=BUS&511;
    /* Permit the observed inherited OC multiplier at ratio 3/4 for this
     * Sony call only. The direct writer's stricter validation stays intact. */
    int known=(num==9 && den==1) ||
        (den==OC_DEN && num>=OC_NORMAL_NUM && num<=oc_numerator(471));
    if(!result && (index<3 || index>5 || !cn || !bn || cn>cd || bn>bd || !known))result=-4;
    if(result)OC_RESULT(result);
    oc_clock_unlock(g);
    if(result)return result;
    if(!running || suspended)return OC_RESULT(-2);
    OC_STEP(OC_D_SONY_CALL,333);
    sony_baseline_result=scePowerSetClockFrequency(333,333,166);
    OC_STEP(OC_D_SONY_RETURN,333);OC_RESULT(sony_baseline_result);
    snapshot("clock_sony_baseline_result");
    if(!running || suspended)return OC_RESULT(-2);
    if(sony_baseline_result<0)return sony_baseline_result;
    /* ARK may return success without changing any registers. apply() reads
     * and validates the real state, and still refuses an unsafe mixed state. */
    return apply();
}

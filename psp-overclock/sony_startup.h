/* SPDX-License-Identifier: MIT
 * Reference-style Sony baseline, once at startup, before direct PLL writes.
 * Never call Sony with interrupts/dispatch locked or during app profiles/exit.
 */
/* Startup-only recovery of the inherited denominator-20 state observed when
 * a patched Sony setter returns success without restoring ratio 5. Never
 * raise the ratio while its OC numerator is still installed. Reduce ONLY
 * the numerator, retaining denominator, upper PLL bits and both domains.
 * This preparation has not acquired normal ratio-5 ownership: cancellation
 * leaves the reduced state in place rather than attempting an unsafe restore.
 */
static int startup_reduce_inherited(void) {
    OcClockGuard g=oc_clock_lock();
    unsigned int ctl=CTL,mul=MUL,cpu=CPU,bus=BUS;
    unsigned int index=ctl&15,num=(mul>>8)&255;
    int eligible=(ctl==3 || ctl==4) && (mul&0xffff00ffU)==0x01240014U &&
        num>OC_NORMAL_NUM && num<=oc_numerator(471) &&
        cpu==0x01ff01ffU && bus==0x01ff01ffU;
    oc_clock_unlock(g);
    if(!eligible)return 0; /* Existing raw validation handles other states. */
    OC_STEP(OC_D_INHERITED_REDUCE,index);
    snapshot("clock_inherited_reduce_begin");
    while(num>OC_NORMAL_NUM) {
        if(!running || suspended)return OC_RESULT(-2);
        g=oc_clock_lock();
        OC_STEP(OC_D_INHERITED_REDUCE,num-1);
        if(CTL!=ctl || MUL!=mul || CPU!=cpu || BUS!=bus) {
            int result=OC_RESULT(-3);oc_clock_unlock(g);return result;
        }
        multiplier(--num);settle();
        mul=(mul&0xffff0000U)|(num<<8)|OC_DEN;
        if(CTL!=ctl || MUL!=mul || CPU!=cpu || BUS!=bus) {
            int result=OC_RESULT(-3);oc_clock_unlock(g);return result;
        }
        oc_clock_unlock(g);
        sceKernelDelayThreadCB(10000);
    }
    if(!running || suspended)return OC_RESULT(-2);
    /* Include the last yield in the ownership checks, before handing over. */
    g=oc_clock_lock();
    int result=OC_RESULT(CTL==ctl && MUL==mul && CPU==cpu && BUS==bus?0:-3);
    oc_clock_unlock(g);
    if(!result)snapshot("clock_inherited_reduce_ready");
    return result;
}
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
    /* Successful setters keep their existing path. Only a recognized
     * inherited denominator-20 state gets downward-only preparation. */
    result=startup_reduce_inherited();
    if(result)return result;
    return apply();
}

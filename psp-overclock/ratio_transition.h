/* SPDX-License-Identifier: MIT
 * Call with interrupts disabled, after ready(), on validated 03/04/05 ratios.
 * Follow ARK-5's adjacent-index transition, but bound and verify every step.
 * The writer is separate so the host test observes the exact hardware writes. */
static int oc_ratio_to_five(int (*checkpoint)(unsigned int,void *),void *context) {
    unsigned int index=CTL&15;
    if(index<3 || index>5)return -4;
    if(index==5)return 0;
    /* Re-latch the starting index, then advance by one: 3, 4, 5.
     * The caller leaves the original multiplier intact throughout this
     * baseline preparation; denominator conversion happens only at 5. */
    for(unsigned int next=index;next<=5;next++) {
        oc_ratio_write((CTL&0xffffff00U)|0x80U|next);
        SYNC();
        if(ready()<0)return -1;
        if((CTL&0x8f)!=next)return -3;
        settle();
        if(checkpoint) {
            int result=checkpoint(next,context);
            if(result)return result;
        }
    }
    return 0;
}

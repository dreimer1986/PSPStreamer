/* SPDX-License-Identifier: MIT
 * PLL constants/formula from m-c/d's Experimental Overclock Stress Tester.
 * Register-derived estimates are NOT a benchmark or stability test. */
#pragma once
#define OC_DEN 20u
#define OC_BASE 37u
#define OC_NORMAL_NUM 180u
static unsigned int oc_numerator(unsigned int mhz) {
    return mhz * OC_DEN / OC_BASE;
}
static unsigned int oc_khz(unsigned int control,unsigned int multiplier,unsigned int domain) {
    unsigned int den=multiplier&255, num=(multiplier>>8)&255;
    unsigned int d=domain&511, n=(domain>>16)&511;
    if((control&0x8f)!=5 || !den || !d || !n) return 0;
    return (unsigned int)(37000ULL*num*n/(den*d));
}
static int oc_supported_model(int model) {
    /* Conservative whitelist: 01g, 02g, 03g, 05g (Go).
     * Later 3000 revisions and Street need different validated recipes. */
    return model==0 || model==1 || model==2 || model==4;
}

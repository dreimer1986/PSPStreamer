/* SPDX-License-Identifier: MIT
 * Direct clock transitions. Included by the PSP worker and host harness.
 * CP0 protection/PLL normalization follow the reference; low-frequency
 * domain targets are a bounded adaptation, not a reference stress test.
 */
static unsigned int owned_ctl,owned_mul,owned_cpu,owned_bus;
static void oc_remember(void);
/* Persist checkpoints only at settled boundaries, outside CP0/dispatch
 * locks. Recheck all registers after I/O: it can run other clock owners.
 * Always return with the caller's guard held, including failure paths. */
static int oc_clock_checkpoint(const char *event,OcClockGuard *guard) {
    unsigned int ctl=CTL,mul=MUL,cpu=CPU,bus=BUS;
    oc_clock_unlock(*guard);
    snapshot(event);
    *guard=oc_clock_lock();
    if(!running || suspended)return -2;
    return CTL==ctl && MUL==mul && CPU==cpu && BUS==bus?0:-3;
}
static int oc_ratio_checkpoint(unsigned int index,void *context) {
    oc_remember();
    const char *event=index==3?"clock_raw_ratio_3_ready":
        index==4?"clock_raw_ratio_4_ready":"clock_raw_ratio_5_ready";
    return oc_clock_checkpoint(event,(OcClockGuard *)context);
}
static void oc_remember(void) {
    owned_ctl=CTL;owned_mul=MUL;owned_cpu=CPU;owned_bus=BUS;changed=1;
}
static int oc_owned(void) {
    return changed && CTL==owned_ctl && MUL==owned_mul &&
        CPU==owned_cpu && BUS==owned_bus;
}
static unsigned int oc_domain(unsigned int mhz,int bus) {
    unsigned int n=mhz>=333?511:mhz*511/(333*(bus?2:1));
    return (n<<16)|511;
}
static int matches(void) {
    unsigned int num=target<333?OC_NORMAL_NUM:oc_numerator(target);
    return (CTL&0x8f)==5 && (MUL&0xffff)==((num<<8)|OC_DEN) &&
        (CPU&0x01ff01ff)==oc_domain(target,0) &&
        (BUS&0x01ff01ff)==oc_domain(target,1);
}
static unsigned int oc_step(unsigned int n,unsigned int goal) {
    if(n<goal)return goal-n>18?n+18:goal;
    return n-goal>18?n-18:goal;
}
/* Caller owns the clock lock. Preserve reserved domain bits. */
static void oc_domains(unsigned int cpu,unsigned int bus) {
    unsigned int cn=(CPU>>16)&511,cd=CPU&511,bn=(BUS>>16)&511,bd=BUS&511;
    unsigned int cgoal=(cpu>>16)&511,bgoal=(bus>>16)&511;
    do {
        cn=oc_step(cn,cgoal);cd=oc_step(cd,511);
        bn=oc_step(bn,bgoal);bd=oc_step(bd,511);
        CPU=(CPU&~0x01ff01ffU)|(cn<<16)|cd;
        BUS=(BUS&~0x01ff01ffU)|(bn<<16)|bd;
        SYNC();settle();
    } while(cn!=cgoal || cd!=511 || bn!=bgoal || bd!=511);
}
/* Restore only our exact last state, including partial ramps. No Sony calls:
 * ARK may replace those with successful no-ops or use a stale cached clock. */
static int restore(void) {
    if(suspended)return -2;
    OcClockGuard g=oc_clock_lock();
    if(!oc_owned() || (CTL&0x8f)!=5 || (MUL&255)!=OC_DEN) {
        oc_clock_unlock(g);return -3;
    }
    unsigned int num=(MUL>>8)&255;
    if(num<OC_NORMAL_NUM || num>oc_numerator(471)) {
        oc_clock_unlock(g);return -4;
    }
    while(num>OC_NORMAL_NUM) {multiplier(--num);settle();}
    oc_domains(0x01ff01ff,0x01ff01ff);
    oc_remember();
    int valid=(MUL&0xffff)==((OC_NORMAL_NUM<<8)|OC_DEN) &&
        (CPU&0x01ff01ff)==0x01ff01ff && (BUS&0x01ff01ff)==0x01ff01ff;
    oc_clock_unlock(g);
    return valid?0:-3;
}
static int apply(void) {
    if(!running || suspended)return -2;
    if(target<66 || target>471)return -4;
    if(changed) {
        int result=restore();
        if(result<0)return result;
    }
    snapshot("clock_raw_baseline_begin");
    OcClockGuard g=oc_clock_lock();
    /* Logging yields to other clock owners too. Recheck after that window. */
    if(changed && !oc_owned()){oc_clock_unlock(g);return -3;}
    int result=ready();
    unsigned int index=CTL&15,num=(MUL>>8)&255,den=MUL&255;
    unsigned int cn=(CPU>>16)&511,cd=CPU&511,bn=(BUS>>16)&511,bd=BUS&511;
    /* Accept the recorded stock 9/1 and our bounded numerator/20 recipe.
     * Never invent a frequency formula for unknown PLL ratio indices. */
    int known_multiplier=(num==9 && den==1) ||
        (den==OC_DEN && num>=OC_NORMAL_NUM && num<=oc_numerator(471) &&
         (index==5 || num==OC_NORMAL_NUM));
    if(!result && (index<3 || index>5 || !cn || !bn || cn>cd || bn>bd ||
                   !known_multiplier))result=-4;
    if(!result)result=oc_clock_checkpoint("clock_raw_guard_ready",&g);
    if(!result) {
        /* Reduce an existing recognized overclock before opening dividers. */
        if(den==OC_DEN)while(num>OC_NORMAL_NUM){multiplier(--num);settle();}
        multiplier(OC_NORMAL_NUM);settle();
        oc_remember();
        result=oc_clock_checkpoint("clock_raw_multiplier_ready",&g);
        if(!result)result=oc_ratio_to_five(oc_ratio_checkpoint,&g);
        if(!result) {
            oc_domains(0x01ff01ff,0x01ff01ff);
            if((CPU&0x01ff01ff)!=0x01ff01ff || (BUS&0x01ff01ff)!=0x01ff01ff ||
               (MUL&0xffff)!=((OC_NORMAL_NUM<<8)|OC_DEN))result=-3;
        }
        if(!result)oc_remember();
    }
    oc_clock_unlock(g);
    if(result)return result;
    snapshot("clock_domains_ready");
    if(target<333) {
        if(!running || suspended)return -2;
        g=oc_clock_lock();
        if(!oc_owned()){oc_clock_unlock(g);return -3;}
        oc_domains(oc_domain(target,0),oc_domain(target,1));
        oc_remember();
        oc_clock_unlock(g);
    } else {
        unsigned int wanted=oc_numerator(target);
        for(num=OC_NORMAL_NUM+1;num<=wanted;num++) {
            if(!running || suspended)return -2;
            g=oc_clock_lock();
            if(!oc_owned()){oc_clock_unlock(g);return -3;}
            multiplier(num);settle();
            result=(MUL&0xffff)==((num<<8)|OC_DEN)?0:-3;
            oc_remember();oc_clock_unlock(g);
            if(result)return result;
            sceKernelDelayThreadCB(10000);
            if(report && ((num-OC_NORMAL_NUM)%16==0 || num==wanted))snapshot("clock_ramp_progress");
        }
    }
    return matches()?0:-3;
}

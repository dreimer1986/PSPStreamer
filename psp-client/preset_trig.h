/* Exact memoization, not a lookup-table approximation. Renderer thread only.
 * Two entries per set prevent a changing point argument from immediately
 * evicting a repeatedly used rotation angle. sin/cos share the argument key. */
#ifndef PSPSTREAMER_PRESET_TRIG_H
#define PSPSTREAMER_PRESET_TRIG_H
enum { PM_TRIG_SETS=32 };
typedef struct { uint32_t key; float sine,cosine; unsigned int valid; } PmTrigEntry;
static PmTrigEntry pm_trig_entries[PM_TRIG_SETS][2];
static unsigned char pm_trig_victim[PM_TRIG_SETS];
static void pm_trig_reset(void) {
    memset(pm_trig_entries,0,sizeof(pm_trig_entries));
    memset(pm_trig_victim,0,sizeof(pm_trig_victim));
}
static float pm_trig(float argument,int cosine) {
    uint32_t key;memcpy(&key,&argument,sizeof(key));
    uint32_t hash=key^(key>>16);
    hash*=0x7feb352dU;hash^=hash>>15;
    unsigned int set=hash&(PM_TRIG_SETS-1),bit=cosine?2U:1U;
    PmTrigEntry *entries=pm_trig_entries[set];
    int slot;
    if(entries[0].valid && entries[0].key==key)slot=0;
    else if(entries[1].valid && entries[1].key==key)slot=1;
    else {
        slot=pm_trig_victim[set];
        entries[slot].key=key;entries[slot].valid=0;
    }
    pm_trig_victim[set]=(unsigned char)(1-slot);
    PmTrigEntry *entry=&entries[slot];
    if(!(entry->valid&bit)) {
        float result=cosine?cosf(argument):sinf(argument);
        /* Do not retain exceptional results. Normal VM input is finite. */
        if(!isfinite(result))return result;
        if(cosine)entry->cosine=result;else entry->sine=result;
        entry->valid|=bit;
    }
    return cosine?entry->cosine:entry->sine;
}
#endif

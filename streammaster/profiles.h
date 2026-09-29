/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef STREAMMASTER_PROFILES_H
#define STREAMMASTER_PROFILES_H
#include "protocol.h"
static inline void sm_profiles_init(SmProfiles *p) {
    memset(p,0,sizeof(*p));p->version=1;p->automatic=1;
}
static inline int sm_profiles_valid(const SmProfiles *p) {
    if(p->version!=1 || p->active>=SM_PROFILE_COUNT || p->automatic>1)return 0;
    for(unsigned i=0;i<SM_PROFILE_COUNT;i++)
        if(p->slot[i].ssid[0] && !sm_config_valid(&p->slot[i],0))return 0;
    return 1;
}
static inline void sm_profiles_public(SmProfiles *p) {
    for(unsigned i=0;i<SM_PROFILE_COUNT;i++) {
        if(p->slot[i].password[0])p->slot[i].flags|=SM_CFG_HAS_PASSWORD;
        memset(p->slot[i].password,0,sizeof(p->slot[i].password));
    }
}
static inline int sm_profile_update(SmProfiles *p,unsigned slot,const SmConfig *draft) {
    if(slot>=SM_PROFILE_COUNT || !sm_config_valid(draft,1))return SM_INVALID;
    SmConfig next=*draft;
    if(next.flags&SM_CFG_KEEP_PASSWORD) {
        /* Never reuse a password from another slot or a different SSID. */
        if(strcmp(next.ssid,p->slot[slot].ssid))return SM_INVALID;
        memcpy(next.password,p->slot[slot].password,sizeof(next.password));
    }
    next.flags&=~SM_CFG_KEEP_PASSWORD;
    p->slot[slot]=next;p->active=slot;memset(&next,0,sizeof(next));return SM_OK;
}
/* RSSI ties prefer the selected slot. Hidden SSIDs fall back to direct connect. */
static inline int sm_profiles_pick(const SmProfiles *p,const SmScan *scan,unsigned tried) {
    int best=-1,rssi=-128;
    for(unsigned k=0;k<SM_PROFILE_COUNT;k++) {
        unsigned i=(p->active+k)%SM_PROFILE_COUNT;
        if(!p->slot[i].ssid[0] || (tried&(1U<<i)))continue;
        for(unsigned j=0;j<scan->count && j<24;j++)
            if(!strcmp(p->slot[i].ssid,scan->ap[j].ssid) && scan->ap[j].rssi>rssi) {
                best=(int)i;rssi=scan->ap[j].rssi;
            }
    }
    if(best>=0)return best;
    for(unsigned k=0;k<SM_PROFILE_COUNT;k++) {
        unsigned i=(p->active+k)%SM_PROFILE_COUNT;
        if(p->slot[i].ssid[0] && !(tried&(1U<<i)))return (int)i;
    }
    return -1;
}
#endif

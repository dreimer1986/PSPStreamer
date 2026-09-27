/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "../streammaster/protocol.h"
#include <stdio.h>
static inline int sm_field_dhcp(const SmConfig *draft,int field) {
    return field>=0 && field<5 && (draft->flags&SM_CFG_DHCP) &&
        (field<3 || (draft->flags&SM_CFG_AUTO_DNS));
}
static inline void sm_field_value(char *out,size_t size,const SmConfig *draft,
                                 const SmNetworkInfo *live,int field) {
    const char *manual[]={draft->ip,draft->mask,draft->gateway,draft->dns,draft->dns2};
    const char *actual[]={live->info.ip,live->mask,live->info.gateway,live->info.dns,live->info.dns2};
    if(field<0 || field>=5){snprintf(out,size,"-");return;}
    if(!sm_field_dhcp(draft,field)){snprintf(out,size,"%s",manual[field]);return;}
    int valid=live->info.wifi_state==SM_WIFI_READY && !strcmp(draft->ssid,live->ssid) &&
        (live->config_flags&SM_CFG_DHCP) && (field<3 || (live->config_flags&SM_CFG_AUTO_DNS));
    const char *value=valid && actual[field][0] && strcmp(actual[field],"0.0.0.0")?actual[field]:"-";
    snprintf(out,size,"(DHCP) %s",value);
}

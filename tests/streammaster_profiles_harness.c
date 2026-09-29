/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "streammaster/profiles.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    SmProfiles p;sm_profiles_init(&p);
    assert(sm_profiles_valid(&p));
    SmConfig c={.flags=SM_CFG_DHCP|SM_CFG_AUTO_DNS};
    strcpy(c.ssid,"home");strcpy(c.password,"home-secret");
    assert(sm_profile_update(&p,0,&c)==SM_OK);
    strcpy(c.ssid,"mobile");strcpy(c.password,"mobile-secret");
    assert(sm_profile_update(&p,1,&c)==SM_OK);
    assert(sm_profile_update(&p,5,&c)==SM_INVALID);
    SmProfiles safe=p;sm_profiles_public(&safe);
    for(unsigned i=0;i<5;i++)assert(!safe.slot[i].password[0]);
    assert(safe.slot[0].flags&SM_CFG_HAS_PASSWORD);
    c=safe.slot[0];c.flags&=~SM_CFG_HAS_PASSWORD;c.flags|=SM_CFG_KEEP_PASSWORD;
    assert(sm_profile_update(&p,0,&c)==SM_OK);
    assert(!strcmp(p.slot[0].password,"home-secret"));
    assert(!strcmp(p.slot[1].password,"mobile-secret"));
    assert(sm_profile_update(&p,1,&c)==SM_INVALID);
    strcpy(c.ssid,"other");assert(sm_profile_update(&p,0,&c)==SM_INVALID);
    SmScan scan={.count=3};
    strcpy(scan.ap[0].ssid,"home");scan.ap[0].rssi=-75;
    strcpy(scan.ap[1].ssid,"mobile");scan.ap[1].rssi=-45;
    strcpy(scan.ap[2].ssid,"home");scan.ap[2].rssi=-35;
    assert(sm_profiles_pick(&p,&scan,0)==0); /* strongest BSSID of same SSID */
    assert(sm_profiles_pick(&p,&scan,1)==1);
    assert(sm_profiles_pick(&p,&scan,3)==-1);
    scan.count=0;assert(sm_profiles_pick(&p,&scan,0)==0); /* hidden fallback */
    p.active=1;assert(sm_profiles_pick(&p,&scan,0)==1);
    scan.count=2;scan.ap[0].rssi=scan.ap[1].rssi=-50;
    assert(sm_profiles_pick(&p,&scan,0)==1); /* preferred tie */
    memset(&p.slot[1],0,sizeof(p.slot[1]));assert(sm_profiles_pick(&p,&scan,0)==0);
    assert(sm_profiles_valid(&p));
    p.active=5;assert(!sm_profiles_valid(&p));p.active=0;
    p.automatic=2;assert(!sm_profiles_valid(&p));p.automatic=1;
    p.version=2;assert(!sm_profiles_valid(&p));p.version=1;
    memset(p.slot[0].ssid,'x',sizeof(p.slot[0].ssid));assert(!sm_profiles_valid(&p));
    _Static_assert(sizeof(SmProfiles)<=SM_PAYLOAD_SIZE,"Profile reply fits USB frame");
    _Static_assert(sizeof(SmProfileSave)<=SM_PAYLOAD_SIZE,"Profile save fits USB frame");
    puts("Five profiles: selection, hidden fallback, validation and password isolation OK");
}

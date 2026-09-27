#include "../streammaster/protocol.h"
#include <assert.h>
#include <stdio.h>
static SmConfig dhcp(void) {
    SmConfig c={.flags=SM_CFG_DHCP|SM_CFG_AUTO_DNS};
    strcpy(c.ssid,"Network");strcpy(c.password,"example-password");return c;
}
int main(void) {
    SmFrame f={.op=SM_ECHO,.sequence=42,.length=SM_PAYLOAD_SIZE};
    for(unsigned i=0;i<SM_PAYLOAD_SIZE;i++)f.payload[i]=(unsigned char)i;
    sm_seal(&f);assert(sm_valid(&f));
    /* Header and payload corruption, including oversized lengths, are rejected
     * before any checksum access can leave the frame. */
    unsigned char *p=(unsigned char *)&f;
    for(unsigned i=0;i<sizeof(f);i++){p[i]^=1;assert(!sm_valid(&f));p[i]^=1;}
    f.length=UINT32_MAX;assert(!sm_valid(&f));
    f.length=0;f.flags=SM_REPLY;sm_seal(&f);assert(sm_valid(&f));
    f.flags=2;sm_seal(&f);assert(!sm_valid(&f));
    SmConfig c=dhcp();assert(sm_config_valid(&c,0));
    c.password[0]=0;assert(sm_config_valid(&c,0));
    c.flags|=SM_CFG_KEEP_PASSWORD;assert(sm_config_valid(&c,1));assert(!sm_config_valid(&c,0));
    c=dhcp();strcpy(c.password,"short");assert(!sm_config_valid(&c,0));
    memset(c.password,'a',64);c.password[64]=0;assert(sm_config_valid(&c,0));
    c.password[30]='z';assert(!sm_config_valid(&c,0));
    c=dhcp();memset(c.ssid,'x',sizeof(c.ssid));assert(!sm_config_valid(&c,0));
    c=dhcp();c.flags=0;strcpy(c.ip,"192.168.10.20");strcpy(c.mask,"255.255.255.0");
    strcpy(c.gateway,"192.168.10.1");strcpy(c.dns,"192.168.10.1");assert(sm_config_valid(&c,0));
    strcpy(c.ip,"192.168.10.255");assert(!sm_config_valid(&c,0));
    strcpy(c.ip,"192.168.10.0");assert(!sm_config_valid(&c,0));
    strcpy(c.ip,"192.168.10.20");strcpy(c.gateway,"192.168.11.1");assert(!sm_config_valid(&c,0));
    strcpy(c.gateway,"192.168.10.20");assert(!sm_config_valid(&c,0));
    strcpy(c.gateway,"0.0.0.0");assert(sm_config_valid(&c,0));
    strcpy(c.mask,"255.0.255.0");assert(!sm_config_valid(&c,0));
    strcpy(c.mask,"255.255.255.255");assert(!sm_config_valid(&c,0));
    strcpy(c.mask,"255.255.255.0");strcpy(c.dns,"0.0.0.0");assert(!sm_config_valid(&c,0));
    strcpy(c.dns,"1.1.1.1");strcpy(c.dns2,"bad");assert(!sm_config_valid(&c,0));
    c.dns2[0]=0;c.flags=SM_CFG_AUTO_DNS;assert(!sm_config_valid(&c,0));
    const char *bad[]={"","1","1.2.3","1.2.3.4.5","256.1.2.3","1..2.3","-1.2.3.4","1.2.3.4x"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(!sm_ip(bad[i],NULL));
    uint32_t ip;assert(sm_ip("192.168.10.20",&ip)&&ip==0xc0a80a14U);
    puts("StreamMaster protocol/config checks passed");return 0;
}

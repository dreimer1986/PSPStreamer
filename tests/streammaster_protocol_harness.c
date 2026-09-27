#include "../streammaster/protocol.h"
#include "../streammaster/main/led_pattern.h"
#include "../psp-client/streammaster_fields.h"
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
    f.flags=SM_COMPACT;sm_seal(&f);assert(sm_valid(&f));
    f.flags=3;sm_seal(&f);assert(!sm_valid(&f));
    for(unsigned n=0;n<=SM_PAYLOAD_SIZE;n++) {
        unsigned wire=sm_wire_size(n);
        assert(wire>=32+n && wire<=SM_FRAME_SIZE);
        assert(wire==SM_FRAME_SIZE || wire%64);
        f.length=n;f.flags=SM_COMPACT;sm_seal(&f);
        assert(sm_request_wire_valid(&f,wire));
        assert(!sm_request_wire_valid(&f,wire-1));
        f.flags=0;sm_seal(&f);assert(sm_request_wire_valid(&f,SM_FRAME_SIZE));
        assert(!sm_request_wire_valid(&f,31));
    }
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
    assert(sm_led_bars(-90,0)==1);assert(sm_led_bars(-80,0)==2);
    assert(sm_led_bars(-70,0)==3);assert(sm_led_bars(-60,0)==4);
    assert(sm_led_bars(-62,4)==4);assert(sm_led_bars(-64,4)==3);
    assert(sm_led_bars(-73,3)==3);assert(sm_led_bars(-74,3)==2);
    assert(sm_led_bars(-95,4)==1);assert(sm_led_bars(-45,1)==4);
    uint8_t rgb[6][3];
    for(unsigned state=0;state<=SM_WIFI_FAILED;state++)for(unsigned phase=0;phase<16;phase++) {
        sm_led_pattern(state,3,1,phase,rgb);
        assert(rgb[0][0]==8&&rgb[0][1]==8&&rgb[0][2]==8);
        assert(rgb[5][0]==0&&rgb[5][1]==8&&rgb[5][2]==24);
        for(int i=0;i<6;i++)for(int j=0;j<3;j++)assert(rgb[i][j]<=24);
    }
    for(int bars=0;bars<=4;bars++) {
        sm_led_pattern(SM_WIFI_READY,bars,0,0,rgb);
        assert(!rgb[5][0]&&!rgb[5][1]&&!rgb[5][2]);
        for(int i=1;i<=4;i++)assert((rgb[i][1]!=0)==(i<=bars));
    }
    sm_led_pattern(SM_WIFI_CONNECTING,0,0,2,rgb);
    assert(rgb[3][0]==24&&rgb[1][0]==0&&rgb[2][0]==0&&rgb[4][0]==0);
    sm_led_pattern(SM_WIFI_FAILED,0,0,0,rgb);assert(rgb[1][0]==24);
    sm_led_pattern(SM_WIFI_FAILED,0,0,2,rgb);assert(rgb[1][0]==3);
    c=dhcp();strcpy(c.ip,"10.0.0.55");strcpy(c.dns,"9.9.9.9");
    SmConfig unchanged=c;
    SmNetworkInfo live={.config_flags=SM_CFG_DHCP|SM_CFG_AUTO_DNS};
    live.info.wifi_state=SM_WIFI_READY;strcpy(live.ssid,c.ssid);
    strcpy(live.info.ip,"192.168.1.12");strcpy(live.mask,"255.255.255.0");
    strcpy(live.info.gateway,"192.168.1.1");strcpy(live.info.dns,"192.168.1.1");
    char display[40];
    sm_field_value(display,sizeof(display),&c,&live,0);assert(!strcmp(display,"(DHCP) 192.168.1.12"));
    sm_field_value(display,sizeof(display),&c,&live,1);assert(!strcmp(display,"(DHCP) 255.255.255.0"));
    sm_field_value(display,sizeof(display),&c,&live,2);assert(!strcmp(display,"(DHCP) 192.168.1.1"));
    sm_field_value(display,sizeof(display),&c,&live,3);assert(!strcmp(display,"(DHCP) 192.168.1.1"));
    sm_field_value(display,sizeof(display),&c,&live,4);assert(!strcmp(display,"(DHCP) -"));
    assert(!memcmp(&c,&unchanged,sizeof(c)));
    c.flags=SM_CFG_DHCP;
    sm_field_value(display,sizeof(display),&c,&live,3);assert(!strcmp(display,"9.9.9.9"));
    c.flags=0;sm_field_value(display,sizeof(display),&c,&live,0);assert(!strcmp(display,"10.0.0.55"));
    c=unchanged;live.info.wifi_state=SM_WIFI_FAILED;
    sm_field_value(display,sizeof(display),&c,&live,0);assert(!strcmp(display,"(DHCP) -"));
    live.info.wifi_state=SM_WIFI_READY;strcpy(live.ssid,"Other network");
    sm_field_value(display,sizeof(display),&c,&live,0);assert(!strcmp(display,"(DHCP) -"));
    strcpy(live.ssid,c.ssid);live.config_flags=0;
    sm_field_value(display,sizeof(display),&c,&live,0);assert(!strcmp(display,"(DHCP) -"));
    puts("StreamMaster protocol/config/LED/DHCP display checks passed");return 0;
}

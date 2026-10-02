/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "../psp-controller/pops_serial.h"
static unsigned calls, last_index, last_port, last_byte;
static int original(unsigned i,unsigned p,unsigned b) {
    ++calls; last_index=i; last_port=p; last_byte=b; return 0xff;
}
static uint8_t native[96];
static PopsSerial ctx;
static void reset(void) {
    memset(&ctx,0,sizeof(ctx)); memset(native,0,sizeof(native)); calls=0;
    ctx.original=original;ctx.native=native;ctx.enabled=1;
    for(unsigned p=0;p<2;p++) {
        uint8_t *s=native+p*48;
        s[0]=0x12;s[1]=0x34;s[0x21]=1;s[0x22]=s[0x24]=0x41;s[0x23]=2;
    }
}
static void command(unsigned cmd,int expected_id) {
    assert(pops_serial(0,0,1,&ctx)==0xff);
    assert(pops_serial(1,0,cmd,&ctx)==expected_id);
    assert(pops_serial(2,0,0,&ctx)==0x5a);
}
int main(void) {
    reset();
    command(0x42,0x41);
    assert(pops_serial(3,0,0,&ctx)==0x12);
    assert(pops_serial(4,0,0,&ctx)==0x34);
    assert(pops_serial(5,0,0,&ctx)==0x100);
    assert(calls==1);
    command(0x43,0x41);
    assert(pops_serial(3,0,1,&ctx)==0x12);
    assert(pops_serial(4,0,0,&ctx)==0x34);
    assert(pops_serial(5,0,0,&ctx)==0x100); /* old length for this transaction */
    assert(ctx.port[0].config==1);
    command(0x45,0xf3);
    const int model[]={1,1,0,2,1,0};
    for(unsigned i=0;i<6;i++)assert(pops_serial(i+3,0,0,&ctx)==model[i]);
    command(0x4d,0xf3);
    for(unsigned i=0;i<6;i++)assert(pops_serial(i+3,0,i<2?i:255,&ctx)==255);
    assert(ctx.port[0].map_enable==3 && ctx.port[0].map_large==2);
    command(0x43,0xf3);
    for(unsigned i=0;i<6;i++)assert(pops_serial(i+3,0,0,&ctx)==0);
    command(0x42,0x41);
    assert(pops_serial(3,0,1,&ctx)==0x12);
    assert(pops_serial(4,0,201,&ctx)==0x34);
    assert(ctx.port[0].small==1 && ctx.port[0].large==201 && ctx.port[0].changes==2);
    pops_serial(4,0,201,&ctx); assert(ctx.port[0].changes==2);
    command(0x44,0x41);pops_serial(3,0,1,&ctx);
    assert(!ctx.port[0].small && !ctx.port[0].large);
    assert(ctx.port[0].peak_small==1 && ctx.port[0].peak_large==201);
    assert(native[0x22]==0x41 && native[0x23]==2); /* no forced analog mode */
    for(unsigned sel=0;sel<3;sel++) {
        command(0x46,0x41);pops_serial(3,0,sel,&ctx);
        assert(pops_serial(5,0,0,&ctx)==(sel<2?1:0));
        assert(pops_serial(8,0,0,&ctx)==(sel==0?10:sel==1?20:0));
    }
    command(0x49,0x41);pops_serial(3,0,1,&ctx);pops_serial(4,0,1,&ctx);pops_serial(5,0,77,&ctx);
    command(0x48,0x41);pops_serial(3,0,1,&ctx);
    assert(pops_serial(7,0,0,&ctx)==1 && pops_serial(8,0,0,&ctx)==77);
    command(0x4c,0x41);assert(pops_serial(6,0,0,&ctx)==4);
    native[0x24]=0x73;assert(pops_serial(6,0,0,&ctx)==7);
    command(0x4a,0x41);pops_serial(3,0,1,&ctx);assert(ctx.port[0].muted==1);
    command(0x4b,0x41);assert(pops_serial(3,0,0,&ctx)==0);
    command(0x47,0x41);assert(pops_serial(5,0,0,&ctx)==2);assert(pops_serial(7,0,0,&ctx)==1);
    assert(ctx.port[0].seen==0xfff);
    assert(!ctx.port[1].commands && !ctx.port[1].small && !ctx.port[1].large);
    /* All command bytes/invalid positions stay in bounds; ports are isolated. */
    for(unsigned cmd=0;cmd<256;cmd++) {
        pops_serial(1,1,cmd,&ctx);
        for(unsigned i=3;i<260;i++)pops_serial(i,1,cmd,&ctx);
        pops_serial(UINT_MAX,1,cmd,&ctx);
    }
    assert(pops_serial(3,2,0,&ctx)==0x1ff);
    ctx.enabled=0;assert(pops_serial(17,1,65,&ctx)==255);
    assert(last_index==17 && last_port==1 && last_byte==65);
    ctx.enabled=1;native[0x21]=255;pops_serial(5,0,66,&ctx);assert(last_byte==66);
    native[0x21]=1;native[0x22]=0x63;pops_serial(3,0,67,&ctx);assert(last_byte==67);
    puts("POPS serial: negotiation, 12 commands, motor capture, bounds and bypass OK");
}

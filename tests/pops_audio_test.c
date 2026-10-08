/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../psp-controller/pops_audio_shared.h"
/* Small instruction-level harness for OUR generated producer, not a POPS
 * emulator. Delay slots in this payload are NOPs except the final SP restore. */
static uint32_t ctx[sizeof(PopsAudioShared)/4],stack[16];
static uint32_t *memory(uint32_t addr) {
    if(addr>=0xa8800000 && addr<0xa8800000+sizeof(ctx))return &ctx[(addr-0xa8800000)/4];
    assert(addr>=0x1000 && addr<0x1040);return &stack[(addr-0x1000)/4];
}
static void run(uint32_t result) {
    uint32_t code[35],r[32]={0};pops_audio_code(code,0x08b93000,0xa8800000);
    r[29]=0x1040;r[31]=0x12345678;r[28]=0x87654321;
    unsigned pc=0,steps=0,called=0;
    while(steps++<100) {
        uint32_t w=code[pc],op=w>>26,rs=(w>>21)&31,rt=(w>>16)&31,rd=(w>>11)&31;
        int16_t imm=w;
        unsigned next=pc+1;
        switch(op) {
        case 0:
            switch(w&63) {
            case 0:r[rd]=r[rt]<<((w>>6)&31);break;
            case 0x21:r[rd]=r[rs]+r[rt];break;
            case 0x23:r[rd]=r[rs]-r[rt];break;
            case 0xf:break;
            case 9:assert(r[rs]==0x08b93000);assert(code[pc+1]==0);r[31]=(pc+2)*4;r[2]=result;r[3]=0xfeedface;called++;next=pc+2;break;
            case 8:
                assert(r[rs]==0x12345678 && code[pc+1]==0x27bd0010);
                assert(r[29]+16==0x1040 && r[28]==0x87654321);
                assert(r[2]==result && r[3]==0xfeedface && called==1);return;
            default:assert(0);
            }break;
        case 4:assert(code[pc+1]==0);next=r[rs]==r[rt]?pc+1+imm:pc+2;break;
        case 9:r[rt]=r[rs]+imm;break;
        case 11:r[rt]=r[rs]<(uint32_t)(int32_t)imm;break;
        case 12:r[rt]=r[rs]&(uint16_t)imm;break;
        case 13:r[rt]=r[rs]|(uint16_t)imm;break;
        case 15:r[rt]=(uint32_t)(uint16_t)imm<<16;break;
        case 35:r[rt]=*memory(r[rs]+imm);break;
        case 43:*memory(r[rs]+imm)=r[rt];break;
        default:assert(0);
        }
        r[0]=0;pc=next;assert(pc<35);
    }
    assert(0);
}
int main(int argc,char **argv) {
    PopsAudioShared *s=(void *)ctx;
    run(0x80007fff);assert(s->wr==0 && s->calls==1);
    s->enabled=1;run(0x1234abcd);assert(s->wr==1 && s->pcm[0]==0x1234abcd);
    s->wr=4096;s->rd=0;run(0xdeadbeef);assert(s->wr==4096 && s->dropped==1);
    s->wr=0xffffffff;s->rd=s->wr;run(0x98765432);assert(s->wr==0 && s->pcm[4095]==0x98765432);
    if(argc==2) {
        FILE *f=fopen(argv[1],"rb");assert(f);uint32_t data[18832/4];
        assert(fread(data,1,sizeof(data),f)==sizeof(data));fclose(f);
        assert(pops_audio_hash(data,0x88181b00,0x2f88,0x31ec)==0x03ea3a65);
        assert(pops_audio_hash(data,0x88181b00,0x3490,0x3514)==0x73444fc4);
        data[0x3124/4]^=1;assert(pops_audio_hash(data,0x88181b00,0x2f88,0x31ec)!=0x03ea3a65);
    }
    puts("POPS producer instruction paths and runtime signatures OK");
}

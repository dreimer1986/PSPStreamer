/* SPDX-License-Identifier: GPL-2.0-or-later
 * Optional binary-fixture test; caller supplies their own decrypted 6.60 PRX.
 * No proprietary image is stored in the repository.
 */
#include <assert.h>
#include <stdio.h>
#include "../psp-controller/pops_signature.h"
int main(int argc,char **argv) {
    assert(argc==2);
    FILE *f=fopen(argv[1],"rb");assert(f);
    uint32_t head[26],call[11];
    assert(!fseek(f,0xc0+0xa250,SEEK_SET));assert(fread(head,4,26,f)==26);
    assert(!fseek(f,0xc0+0x9e88,SEEK_SET));assert(fread(call,4,11,f)==11);fclose(f);
    assert(pops_serial_signature(head,call,0));
    /* Simulate loader relocation at several aligned and unaligned bases. */
    const uint32_t bases[]={0x08800000,0x08804000,0x0890c000};
    for(unsigned b=0;b<3;b++) {
        uint32_t addr=bases[b]+0xa250;
        call[0]=0x3c080000u | ((addr+0x8000u)>>16);
        call[4]=0x25020000u | (addr&65535u);
        assert(pops_serial_signature(head,call,bases[b]));
        for(unsigned i=0;i<26;i++) {
            head[i]^=1;assert(!pops_serial_signature(head,call,bases[b]));head[i]^=1;
        }
        for(unsigned i=0;i<11;i++) {
            call[i]^=1;assert(!pops_serial_signature(head,call,bases[b]));call[i]^=1;
        }
    }
    puts("POPS 6.60 03g: real binary signature, relocation and 111 rejection cases OK");
}

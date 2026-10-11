/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef FTP_SIZE_TEXT_H
#define FTP_SIZE_TEXT_H
#include <stdint.h>
/* Sysclib's unsigned long-long printf path consumes the argument without
 * converting it. Use 32-bit arithmetic, also avoiding kernel libgcc helpers. */
static void ftp_size_text(uint64_t value,char out[21]){
    uint32_t limbs[4]={(uint32_t)(value>>48),(uint32_t)(value>>32)&65535U,
                       (uint32_t)(value>>16)&65535U,(uint32_t)value&65535U};
    char reversed[20];unsigned n=0;
    do {
        uint32_t remainder=0;
        for(unsigned i=0;i<4;i++){
            uint32_t part=(remainder<<16)|limbs[i];
            limbs[i]=part/10;remainder=part%10;
        }
        reversed[n++]=(char)('0'+remainder);
    }while(limbs[0]|limbs[1]|limbs[2]|limbs[3]);
    for(unsigned i=0;i<n;i++)out[i]=reversed[n-i-1];
    out[n]=0;
}
#endif

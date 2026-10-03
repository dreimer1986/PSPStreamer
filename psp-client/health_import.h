/* Convert a single search result/write command, never execute a cheat. */
#ifndef HEALTH_IMPORT_H
#define HEALTH_IMPORT_H
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
static void hi_space(const char **s){while(**s==' '||**s=='\t')++*s;}
static int hi_hex(const char **s,uint32_t *out){
    hi_space(s);if(**s=='-'||**s=='+')return -1;
    char *end;errno=0;unsigned long v=strtoul(*s,&end,16);
    if(errno||end==*s||v>UINT32_MAX)return -1;
    *s=end;*out=(uint32_t)v;return 0;
}
static int hi_address(uint32_t a,int type){unsigned n=type==1?1:type==2?2:4;
    return type>=1&&type<=4&&a>=0x08800000u&&a<=0x0a000000u-n&&!(a&(n-1));}
static int hi_cw(const char *s,uint32_t *address,uint32_t *value,int *width){
    hi_space(&s);if(s[0]=='_'&&s[1]=='L'){s+=2;if(*s!=' '&&*s!='\t')return -1;}
    uint32_t command;if(hi_hex(&s,&command)||(*s!=' '&&*s!='\t')||hi_hex(&s,value))return -1;
    hi_space(&s);if(*s)return -1; /* no multi-line/pointer/conditional scripts */
    unsigned op=command>>28;if(op>2)return -1;
    *width=(int)op+1;*address=0x08800000u+(command&0x0fffffffu);
    if(!hi_address(*address,*width)||(*width==1&&*value>255)||(*width==2&&*value>65535))return -1;
    return 0;
}
static int hi_retro(const char *s,uint32_t *address){
    uint32_t offset;if(hi_hex(&s,&offset))return -1;
    hi_space(&s);if(*s||offset<0x00800000u||offset>=0x02000000u)return -1;
    *address=0x08000000u+offset;return 0;
}
static int hi_maximum(uint32_t raw,int type){
    if(type!=4)return raw>0&&raw<=INT_MAX?(int)raw:0;
    if(!raw||raw>0x4effffffu)return 0; /* reject sign/NaN/Inf before FPU access */
    float f;memcpy(&f,&raw,4);
    if(!(f>0&&f<=2147483520.0f))return 0;
    int n=(int)f;return (float)n<f?n+1:n;
}
#endif

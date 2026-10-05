#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <float.h>
#include <assert.h>
float xbox_strtof(const char *,char **);
double xbox_strtod(const char *,char **);
static void check(const char *s){
    char *a,*b;errno=0;float x=strtof(s,&a);int range=errno==ERANGE;
    errno=0;float y=xbox_strtof(s,&b);int actual_range=errno==ERANGE;
    /* C permits implementation-specific ERANGE signaling for subnormals;
     * musl and glibc differ there. Bits/end pointer must still match exactly. */
    int subnormal=x!=0&&fabsf(x)<FLT_MIN;
    if(!(a-s==b-s&&(isnan(x)?isnan(y):memcmp(&x,&y,4)==0)&&(range==actual_range||subnormal))){
        fprintf(stderr,"conversion mismatch: %s end %ld/%ld range %d/%d values %.9g/%.9g\n",s,(long)(a-s),(long)(b-s),range,actual_range,x,y);abort();
    }
}
int main(void){
    const char *cases[]={"", " ", "+", "-", ".", "-0", " +.5tail", "1e", "1e+", "1e-2x", "0x", "0x.1p2", "0x1p-149", "nan(foo)", "nan(", "infinity", "infinit", "in", "1e9999", "-1e-9999", "3.4028234663852886e38", "1.1754943508222875e-38", "1.401298464324817e-45", "1.000000059604644775390625", "0.00000000000000000000000000000000000000000000000001"};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++)check(cases[i]);
    uint32_t seed=42;char text[128];
    for(int i=0;i<10000;i++){seed=seed*1664525+1013904223;snprintf(text,sizeof(text),"%s%u.%09ue%+d suffix",seed&1?"-":"",seed,seed^0xa3456789,(int)(seed%110)-65);check(text);}
    char *end;assert(xbox_strtod("1e3rest",&end)==1000&&!strcmp(end,"rest"));
    puts("Xbox strtof: boundaries, exponents, hex, invalid input and 10000 reference comparisons passed");
    return 0;
}

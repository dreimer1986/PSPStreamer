/* Use musl's correctly rounded conversion, not SDL's limited decimal scanner.
 * Keep symbols private to the Xbox app to avoid replacing unrelated SDK ABI. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vendor/musl/shgetc.h"
#include "vendor/musl/floatscan.h"
#undef FILE
static long double xbox_strtox(const char *s,char **end,int precision) {
    XboxFloatInput f={(const unsigned char*)s,(const unsigned char*)s,
                      (const unsigned char*)s+strlen(s),0};
    long double value=xbox_floatscan(&f,precision,1);
    if(end)*end=f.invalid?(char*)s:(char*)f.cursor;
    return value;
}
float xbox_strtof(const char *s,char **end){return xbox_strtox(s,end,0);}
double xbox_strtod(const char *s,char **end){return xbox_strtox(s,end,1);}
long double xbox_strtold(const char *s,char **end){return xbox_strtox(s,end,2);}

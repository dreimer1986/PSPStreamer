/* Xbox string-only adapter; no dependency on musl's private FILE ABI. */
#ifndef XBOX_MUSL_STRING_SCAN_H
#define XBOX_MUSL_STRING_SCAN_H
#include <stddef.h>
typedef struct { const unsigned char *start,*cursor,*end; int invalid; } XboxFloatInput;
#define FILE XboxFloatInput
static int xbox_scan_get(XboxFloatInput *f) {
    return f->cursor<=f->end ? *f->cursor++ : -1;
}
#define shgetc(f) xbox_scan_get(f)
#define shunget(f) ((void)((f)->cursor>(f)->start ? --(f)->cursor : (f)->cursor))
#define shlim(f, n) ((void)((f)->invalid=1))
/* nxdk's scalbn* are stubs; with FLT_RADIX=2 ldexp* have identical scaling. */
#define scalbn ldexp
#define scalbnl ldexpl
#define __floatscan xbox_floatscan
#endif

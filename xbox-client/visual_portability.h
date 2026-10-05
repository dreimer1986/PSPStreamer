/* CPU-side shared effect storage never goes directly into an NV2A DMA list.
 * The GPU adapter owns physically contiguous, explicitly aligned copies. */
#ifndef XBOX_VISUAL_PORTABILITY_H
#define XBOX_VISUAL_PORTABILITY_H
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
float xbox_strtof(const char *,char **);
double xbox_strtod(const char *,char **);
long double xbox_strtold(const char *,char **);
#define strtof xbox_strtof
#define strtod xbox_strtod
#define strtold xbox_strtold
/* Shared preset paths deliberately retain '/' for relative texture lookup.
 * Normalize only at the OS boundary; FATX/CreateFile expects backslashes. */
static inline FILE *xbox_visual_fopen(const char *path,const char *mode){
    char native_path[1024];int n=snprintf(native_path,sizeof(native_path),"%s%s",strchr(path,':')?"":"D:\\",path);
    if(n<0||n>=(int)sizeof(native_path)){errno=EINVAL;return NULL;}
    for(char *p=native_path;*p;p++)if(*p=='/')*p='\\';return fopen(native_path,mode);
}
#define fopen xbox_visual_fopen
/* The shared structs themselves have alignment contracts, even without DMA.
 * Replacing memalign with malloc lets LLVM emit MOVAPS to an 8-byte-aligned
 * CaveScene and faults when starting flight. nxdk's dlmalloc aligned_alloc
 * is implemented, free-compatible, and must retain the requested alignment. */
#include "visual_alloc.h"
#define memalign(alignment,size) xbox_visual_memalign(alignment,size)
/* nxdk has no complete fenv implementation. Keep the shared evaluator's scoped
 * non-trapping policy, using Pentium III x87 + SSE1 controls (not SSE2). */
#define PSPSTREAMER_PRESET_FPU_H
typedef struct {unsigned short cw;unsigned mxcsr;} MdFpuState;
static inline MdFpuState md_fpu_enter(void){
    MdFpuState old;__asm__ volatile("fnstcw %0; stmxcsr %1":"=m"(old.cw),"=m"(old.mxcsr));
    unsigned short masked=old.cw|0x3f;unsigned csr=(old.mxcsr|0x1f80)&~0x3fU;
    __asm__ volatile("fldcw %0; ldmxcsr %1"::"m"(masked),"m"(csr):"memory");return old;
}
static inline void md_fpu_leave(MdFpuState *old){
    __asm__ volatile("fnclex; fldcw %0; ldmxcsr %1"::"m"(old->cw),"m"(old->mxcsr):"memory");
}
#endif

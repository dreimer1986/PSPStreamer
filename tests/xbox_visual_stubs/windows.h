#ifndef XBOX_VISUAL_TEST_WINDOWS_H
#define XBOX_VISUAL_TEST_WINDOWS_H
#include <stdint.h>
#include <stdlib.h>
#define PAGE_READWRITE 1
#define PAGE_WRITECOMBINE 2
static unsigned test_clock,test_allocations;
static unsigned GetTickCount(void){return ++test_clock;}
static void Sleep(unsigned delay){test_clock+=delay;}
static void *MmAllocateContiguousMemoryEx(size_t n,unsigned lo,unsigned hi,unsigned align,unsigned flags){
    (void)lo;(void)hi;(void)align;(void)flags;void *p=malloc(n);if(p)test_allocations++;return p;
}
static void MmFreeContiguousMemory(void *p){if(p)test_allocations--;free(p);}
#endif

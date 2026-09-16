#ifndef TLS_HOST_PSPKERNEL_H
#define TLS_HOST_PSPKERNEL_H
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
int sceKernelCreateSema(const char *,int,int,int,void *);
int sceKernelWaitSema(int,int,void *);
int sceKernelSignalSema(int,int);
static inline unsigned long long sceKernelGetSystemTimeWide(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (unsigned long long)t.tv_sec*1000000+t.tv_nsec/1000;
}
#define sceIoMkdir mkdir
#define sceIoRemove unlink
#define sceIoRename rename
#endif

#ifndef PSP_QUEUE_QUALITY_H
#define PSP_QUEUE_QUALITY_H
#include <string.h>
/* Entry overrides must not become the next entry's inherited defaults or CFG. */
typedef struct { int active, quality, fps; } QueueQuality;
static int queue_quality_index(const char *name) {
    static const char *names[]={"96k","128k","160k","v6","v5","v4","v3"};
    for(int i=0;i<7;i++)if(!strcmp(name,names[i]))return i;
    return -1;
}
static void queue_quality_restore(QueueQuality *scope,int *quality,int *fps) {
    if(scope->active){*quality=scope->quality;*fps=scope->fps;scope->active=0;}
}
static void queue_quality_apply(QueueQuality *scope,int *quality,int *fps,int q,int f) {
    if(!scope->active){scope->quality=*quality;scope->fps=*fps;scope->active=1;}
    *quality=q>=0&&q<7?q:scope->quality;
    *fps=f>=0&&f<=1?f:scope->fps;
}
#endif

/* CPU storage still has C/SSE alignment requirements, independently of DMA. */
#ifndef XBOX_VISUAL_ALLOC_H
#define XBOX_VISUAL_ALLOC_H
#include <stdlib.h>
static inline void *xbox_visual_memalign(size_t alignment,size_t size){
    if(!alignment||(alignment&(alignment-1))||size>(size_t)-1-(alignment-1))return NULL;
    return aligned_alloc(alignment,(size+alignment-1)&~(alignment-1));
}
#endif

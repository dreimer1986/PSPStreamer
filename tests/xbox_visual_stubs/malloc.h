/* Exercise the Xbox allocator adapter, not the host's unrelated memalign. */
#include "visual_alloc.h"
#define memalign(alignment,size) xbox_visual_memalign(alignment,size)

/* SPDX-License-Identifier: GPL-2.0-or-later
 * No PSP dependencies: same decoder is exercised by host regression tests. */
#include "milkdrop_texture.h"
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>

void md_image_free(MdImage *image) {
    free(image->pixels); memset(image,0,sizeof(*image));
}
int md_image_load(const char *path, MdImage *out) {
    FILE *file=fopen(path,"rb");
    unsigned char *data=NULL,*pixels=NULL,*swizzled=NULL;
    png_image png; memset(&png,0,sizeof(png)); png.version=PNG_IMAGE_VERSION;
    int ok=0; long length;
    if(!file) return 0;
    if(fseek(file,0,SEEK_END) || (length=ftell(file))<33 || length>1024*1024 ||
       fseek(file,0,SEEK_SET)) goto done;
    data=malloc((size_t)length);
    if(!data || fread(data,1,(size_t)length,file)!=(size_t)length) goto done;
    /* Reject huge headers before invoking the decoder. Native GU dimensions
     * are powers of two; no hidden stretching, padding or downscaling. */
    if(png_sig_cmp(data,0,8) || memcmp(data+12,"IHDR",4)) goto done;
    unsigned int w=png_get_uint_32(data+16),h=png_get_uint_32(data+20);
    if(w<16 || h<16 || w>256 || h>256 || (w&(w-1)) || (h&(h-1))) goto done;
    if(!png_image_begin_read_from_memory(&png,data,(size_t)length)) goto done;
    if(png.width!=w || png.height!=h) goto done;
    png.format=PNG_FORMAT_RGBA;
    pixels=memalign(64,w*h*4);
    if(!pixels || !png_image_finish_read(&png,NULL,pixels,0,NULL)) goto done;
    /* GE's 16-byte x 8-row blocks improve main-RAM texture cache locality.
     * Do this only on activation, not in the per-frame rendering path. */
    swizzled=memalign(64,w*h*4);
    if(!swizzled) goto done;
    for(unsigned int y=0;y<h;y++) for(unsigned int x=0;x<w*4;x+=16) {
        unsigned int block=(y/8)*(w/4)+x/16;
        memcpy(swizzled+block*128+(y%8)*16,pixels+y*w*4+x,16);
    }
    *out=(MdImage){swizzled,w,h}; swizzled=NULL; ok=1;
done:
    png_image_free(&png); free(swizzled); free(pixels); free(data); fclose(file);
    return ok;
}

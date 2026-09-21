/* SPDX-License-Identifier: GPL-2.0-or-later
 * No PSP dependencies: same decoder is exercised by host regression tests. */
#include "milkdrop_texture.h"
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <jpeglib.h>
#include <setjmp.h>

typedef struct {
    struct jpeg_decompress_struct decoder;
    struct jpeg_error_mgr error;
    jmp_buf jump;
    unsigned char *pixels,*row;
} MdJpeg;
static void md_jpeg_error(j_common_ptr decoder) {
    MdJpeg *ctx=(MdJpeg *)decoder->client_data;
    longjmp(ctx->jump,1);
}
static void md_jpeg_message(j_common_ptr decoder,int level) {
    /* Truncated streams must not succeed with libjpeg's synthetic EOI. */
    if(level<0)md_jpeg_error(decoder);
}
static unsigned int md_texture_size(unsigned int size) {
    unsigned int result=16;
    while(result<size && result<256)result*=2;
    return result;
}
static unsigned char *md_jpeg_decode(const unsigned char *data,size_t length,
                                     unsigned int *width,unsigned int *height) {
    /* Heap ownership remains well-defined across libjpeg longjmp failures. */
    MdJpeg *ctx=calloc(1,sizeof(*ctx));
    if(!ctx)return NULL;
    ctx->decoder.err=jpeg_std_error(&ctx->error);
    ctx->error.error_exit=md_jpeg_error;
    ctx->error.emit_message=md_jpeg_message;
    ctx->decoder.client_data=ctx;
    if(setjmp(ctx->jump)) {
        jpeg_destroy_decompress(&ctx->decoder);
        free(ctx->pixels);free(ctx->row);free(ctx);return NULL;
    }
    jpeg_create_decompress(&ctx->decoder);
    jpeg_mem_src(&ctx->decoder,data,length);
    jpeg_read_header(&ctx->decoder,TRUE);
    if(!ctx->decoder.image_width || !ctx->decoder.image_height ||
       ctx->decoder.image_width>1024 || ctx->decoder.image_height>1024 ||
       (ctx->decoder.jpeg_color_space!=JCS_RGB && ctx->decoder.jpeg_color_space!=JCS_YCbCr &&
        ctx->decoder.jpeg_color_space!=JCS_GRAYSCALE))md_jpeg_error((j_common_ptr)&ctx->decoder);
    unsigned int w=md_texture_size(ctx->decoder.image_width),h=md_texture_size(ctx->decoder.image_height);
    ctx->decoder.out_color_space=JCS_RGB;
    ctx->decoder.scale_num=1;
    ctx->decoder.scale_denom=1;
    while(ctx->decoder.scale_denom<8 &&
          ctx->decoder.image_width/(ctx->decoder.scale_denom*2)>=w &&
          ctx->decoder.image_height/(ctx->decoder.scale_denom*2)>=h)
        ctx->decoder.scale_denom*=2;
    jpeg_start_decompress(&ctx->decoder);
    ctx->pixels=memalign(64,w*h*4);
    ctx->row=malloc(ctx->decoder.output_width*3);
    if(!ctx->pixels || !ctx->row)md_jpeg_error((j_common_ptr)&ctx->decoder);
    /* Column selection is identical on every output row. Keep exact integer
     * nearest-neighbour sampling, but divide only once per output column. */
    unsigned int source_x[256];
    for(unsigned int x=0;x<w;x++)source_x[x]=x*ctx->decoder.output_width/w*3;
    unsigned int target_y=0;
    while(ctx->decoder.output_scanline<ctx->decoder.output_height) {
        unsigned int source_y=ctx->decoder.output_scanline;
        JSAMPROW row=ctx->row;
        if(jpeg_read_scanlines(&ctx->decoder,&row,1)!=1)md_jpeg_error((j_common_ptr)&ctx->decoder);
        while(target_y<h && target_y*ctx->decoder.output_height/h==source_y) {
            for(unsigned int x=0;x<w;x++) {
                unsigned char *dest=ctx->pixels+(target_y*w+x)*4;
                memcpy(dest,ctx->row+source_x[x],3);dest[3]=255;
            }
            target_y++;
        }
    }
    jpeg_finish_decompress(&ctx->decoder);
    jpeg_destroy_decompress(&ctx->decoder);
    unsigned char *pixels=ctx->pixels;
    free(ctx->row);free(ctx);
    *width=w;*height=h;return pixels;
}

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
    /* Reject huge headers before decoding. PNG requires native GU dimensions;
     * JPEG is resized to bounded power-of-two dimensions by md_jpeg_decode. */
    unsigned int w=0,h=0;
    if(data[0]==0xff && data[1]==0xd8) {
        pixels=md_jpeg_decode(data,(size_t)length,&w,&h);
        if(!pixels)goto done;
    } else {
    if(png_sig_cmp(data,0,8) || memcmp(data+12,"IHDR",4)) goto done;
    w=png_get_uint_32(data+16);h=png_get_uint_32(data+20);
    if(w<16 || h<16 || w>256 || h>256 || (w&(w-1)) || (h&(h-1))) goto done;
    if(!png_image_begin_read_from_memory(&png,data,(size_t)length)) goto done;
    if(png.width!=w || png.height!=h) goto done;
    png.format=PNG_FORMAT_RGBA;
    pixels=memalign(64,w*h*4);
    if(!pixels || !png_image_finish_read(&png,NULL,pixels,0,NULL)) goto done;
    }
    /* Both decoders have finished using the compressed input. Release it
     * before allocating a second pixel buffer for the GU layout. */
    png_image_free(&png);
    free(data);data=NULL;
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

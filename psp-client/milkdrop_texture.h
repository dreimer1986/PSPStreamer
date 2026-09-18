/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PSPSTREAMER_MILKDROP_TEXTURE_H
#define PSPSTREAMER_MILKDROP_TEXTURE_H
typedef struct { unsigned char *pixels; unsigned int width,height; } MdImage;
/* PNG/JPEG -> aligned, GE-swizzled RGBA. Output unchanged on failure;
 * free before reuse. PNG: power of two, 16..256. JPEG: up to 1024 per
 * dimension, resized to powers of two 16..256, opaque alpha. */
int md_image_load(const char *path, MdImage *out);
void md_image_free(MdImage *image);
#endif

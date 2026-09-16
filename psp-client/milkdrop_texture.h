/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PSPSTREAMER_MILKDROP_TEXTURE_H
#define PSPSTREAMER_MILKDROP_TEXTURE_H
typedef struct { unsigned char *pixels; unsigned int width,height; } MdImage;
/* Bounded PNG -> aligned, GE-swizzled RGBA. Output unchanged on failure;
 * free before reuse. Each dimension: power of two, 16..256. */
int md_image_load(const char *path, MdImage *out);
void md_image_free(MdImage *image);
#endif

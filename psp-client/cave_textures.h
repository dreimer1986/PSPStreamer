/* SPDX-License-Identifier: GPL-2.0-or-later
 * Optional user-owned Monkey textures; never distribute the original images. */
#ifndef PSPSTREAMER_CAVE_TEXTURES_H
#define PSPSTREAMER_CAVE_TEXTURES_H
#include "milkdrop_texture.h"
#include <stdio.h>
enum {CAVE_TEXTURE_BANKS=7};
static int cave_texture_load(const char *directory,int bank,MdImage *image) {
    static const char *extensions[]={"jpg","png","jpeg"};
    if(bank<0 || bank>=CAVE_TEXTURE_BANKS || image->pixels)return 0;
    for(int i=0;i<3;i++) {
        char path[512];
        int n=snprintf(path,sizeof(path),"%s/supertex_%c%d.%s",directory,bank<5?'a':'b',bank<5?bank+1:bank-4,extensions[i]);
        if(n<0 || n>=(int)sizeof(path))return 0;
        if(md_image_load(path,image))return 1;
    }
    return 0;
}
#endif

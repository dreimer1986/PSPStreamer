/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef CONSOLIZER_XMB_TEXTURE_SHARED_H
#define CONSOLIZER_XMB_TEXTURE_SHARED_H
#include <stdint.h>
#define XMB_TEXTURE_RECORDS 16
typedef struct {
    uint32_t result,input_size,input_hash,surface,flags;
    char key[32];
    uint32_t words[26];
} XmbTextureRecord;
typedef struct {
    uint32_t original,ram_end;
    volatile uint32_t enabled,users,busy,produced,dropped;
    XmbTextureRecord records[XMB_TEXTURE_RECORDS];
} XmbTextureShared;
#endif

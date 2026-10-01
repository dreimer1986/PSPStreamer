/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
/* Placement-only experiment, not a promise of cache-disabled/ISR safety.
 * Callees and channel buffers retain their original placement and ownership. */
#if defined(SM_HOT_IRAM) && SM_HOT_IRAM
#include "esp_attr.h"
#define SM_HOT_CODE IRAM_ATTR __attribute__((noinline))
#else
#define SM_HOT_CODE
#endif

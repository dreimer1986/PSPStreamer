/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#define SM_PAD_HOME 0x10000U
#define SM_PAD_WIRE_MASK 0xf3fbU
/* Bit 1 was unused in the 16-bit EP0 button word. HOME itself is bit 16. */
static inline unsigned sm_pad_pack_buttons(unsigned buttons,int home_capable) {
    return (buttons&0xf3f9U)|((home_capable && (buttons&SM_PAD_HOME))?2U:0U);
}
static inline unsigned sm_pad_unpack_buttons(unsigned buttons) {
    return (buttons&0xf3f9U)|((buttons&2U)?SM_PAD_HOME:0U);
}

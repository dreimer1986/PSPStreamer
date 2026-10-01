/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>
enum {SM_PAD_SEND_NONE,SM_PAD_SEND_INPUT,SM_PAD_SEND_META};
static inline int sm_pad_send_kind(int64_t now,int64_t sent,int changed,int capable,int meta_disabled,unsigned chunk,int64_t meta_at){
    int64_t age=now-sent;
    if(age>=20000 && (changed || age>=200000))return SM_PAD_SEND_INPUT;
    if(capable && !meta_disabled && chunk<16 && now>=meta_at && age>=0 && age<100000)return SM_PAD_SEND_META;
    return SM_PAD_SEND_NONE;
}
static inline void sm_pad_send_failed(int metadata,int *input_disabled,int *meta_disabled){
    if(metadata)*meta_disabled=1;else *input_disabled=1;
}

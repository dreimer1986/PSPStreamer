/* SPDX-License-Identifier: MIT
 * Optional, buffer-free sceIoDevctl API. No imports or user pointers cross
 * into the plugin. 0 MHz releases the session override to the INI target. */
#pragma once
#define OC_DEVICE "streameroc:"
#define OC_CMD_STATUS 0x534f0001u
#define OC_CMD_CPU_KHZ 0x534f0002u
#define OC_CMD_TARGET 0x534f0003u
#define OC_CMD_PREPARE_EXIT 0x534f0004u
#define OC_CMD_EXIT_STATUS 0x534f0005u
/* PREPARE_EXIT stops future requests and asks the worker to restore its
 * owned baseline before loadexec. EXIT_STATUS: 1 pending, 0 done, <0 failed. */
#define OC_CMD_SET 0x534f1000u
#define OC_CMD_SET_MASK 0xfffff000u
/* STATUS: 0 ready/applied, 1 pending; negative unavailable/disabled/failed. */
static inline int oc_target_valid(int mhz) {return mhz>=66&&mhz<=471;}

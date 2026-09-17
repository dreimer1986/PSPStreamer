/* SPDX-License-Identifier: MIT */
#ifndef STREAMER_OC_POWER_CALLBACK_SLOT_H
#define STREAMER_OC_POWER_CALLBACK_SLOT_H

/* Kernel plugins cannot rely on automatic slot allocation on every firmware.
 * Explicit registration returns zero, NOT the slot: retain the requested slot.
 * Never unregister occupied slots to make room for this plugin. */
static int oc_register_power_callback(int callback, int *auto_result, int *last_result)
{
    *auto_result = scePowerRegisterCallback(-1, callback);
    *last_result = *auto_result;
    if (*auto_result >= 0)
        return *auto_result;
    for (int slot = 15; slot >= 0; --slot) {
        *last_result = scePowerRegisterCallback(slot, callback);
        if (*last_result >= 0)
            return slot;
    }
    return -1;
}
#endif

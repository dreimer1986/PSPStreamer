/* SPDX-License-Identifier: GPL-2.0-or-later
 * Generic boards have no assumed LED pin or external peripheral wiring. */
#include "bridge.h"
void sm_led_init(void) {}
void sm_led_usb(int attached) {(void)attached;}

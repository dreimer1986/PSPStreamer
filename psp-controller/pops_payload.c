/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "pops_serial.h"
/* First and only function in .text; embedded as position-independent code.
 * The entry stub supplies the context as a3 and preserves POPS's scratch GP. */
int pops_payload(unsigned index, unsigned port, unsigned incoming, PopsSerial *ctx) {
    return pops_serial(index, port, incoming, ctx);
}

/* SPDX-License-Identifier: GPL-2.0-or-later
 * Independently implemented POPS 05g serial-command semantics. See
 * docs/POPS_RUMBLE_BINARY_FINDINGS.md for the inspected instruction paths.
 * No Sony code/data is embedded. This header also runs in host tests.
 */
#ifndef CONSOLIZER_POPS_SERIAL_H
#define CONSOLIZER_POPS_SERIAL_H
#include <stdint.h>
typedef int (*PopsOriginal)(unsigned, unsigned, unsigned);
typedef struct {
    uint8_t command, config, latched_config, selector, write_enable, muted;
    uint8_t map_enable, map_large, small, large;
    uint32_t commands, polls, changes, seen;
    uint8_t peak_small, peak_large;
} PopsPort;
typedef struct {
    PopsOriginal original;
    volatile uint8_t *native;
    volatile uint32_t enabled;
    PopsPort port[2];
} PopsSerial;

/* Force inlining: the copied user payload must have no absolute references,
 * global data, libc calls, GP setup, relocation or kernel calls. */
#define POPS_INLINE static inline __attribute__((always_inline))
POPS_INLINE void pops_motor(PopsPort *p, unsigned motor, uint8_t value) {
    uint8_t *v = motor ? &p->large : &p->small;
    if (*v != value) { *v = value; ++p->changes; }
    if (p->small > p->peak_small) p->peak_small = p->small;
    if (p->large > p->peak_large) p->peak_large = p->large;
}
POPS_INLINE int pops_serial(unsigned index, unsigned port, unsigned incoming, PopsSerial *ctx) {
    if (port >= 2) return 0x1ff;
    volatile uint8_t *s = ctx->native + port * 0x30;
    PopsPort *p = &ctx->port[port];
    if (!ctx->enabled || (int8_t)s[0x21] <= 0)
        return ctx->original(index, port, incoming);
    /* Leave mouse/GunCon/other pad emulation owned by POPS or another plugin. */
    if (!((s[0x22] == 0x41 && s[0x23] == 2) ||
          (s[0x22] == 0x73 && s[0x23] == 6)))
        return ctx->original(index, port, incoming);
    /* Preserve Sony's port/button mapping. Do NOT call it for non-poll
     * commands: the ordinary 03g handler rejects those outright. */
    if (!index) return ctx->original(index, port, incoming);
    uint8_t byte = (uint8_t)incoming;
    if (index == 1) {
        p->command = byte; p->latched_config = p->config; ++p->commands;
        if (byte >= 0x42 && byte <= 0x4d) p->seen |= 1u << (byte - 0x42);
        if (byte == 0x42) ++p->polls;
        return p->latched_config ? 0xf3 : s[0x22];
    }
    if (index == 2) return 0x5a;
    unsigned pos = index - 3;
    unsigned count = p->latched_config ? 6 : s[0x23];
    if (count > 6) return 0x1ff;
    /* POPS asks once beyond the response; bit 8 terminates the transaction.
     * Bound every access, including malformed game input. */
    if (pos >= 6 && p->command != 0x42 && p->command != 0x43) return 0x100;
    switch (p->command) {
    case 0x42:
        if (pos < 6 && (p->map_enable & (1u << pos)))
            pops_motor(p, !!(p->map_large & (1u << pos)), byte);
        return pos < count ? s[pos] : (s[count] | 0x100);
    case 0x43:
        if (!pos) p->config = byte;
        if (p->latched_config) return pos < 6 ? 0 : 0x100;
        return pos < count ? s[pos] : (s[count] | 0x100);
    case 0x44:
        /* Like Go A7DC, keep Sony's selected digital/analog mode; this is
         * not a replacement for POPS's input mapping/settings. */
        if (!pos) { pops_motor(p, 0, 0); pops_motor(p, 1, 0); }
        return 0;
    case 0x45:
        return pos == 0 || pos == 1 || pos == 4 ? 1 : pos == 3 ? 2 : 0;
    case 0x46:
        if (!pos) p->selector = byte;
        if (p->selector > 1) return 0;
        if (pos == 2) return 1;
        if (pos == 3) return p->selector ? 1 : 2;
        if (pos == 4) return p->selector ? 1 : 0;
        return pos == 5 ? (p->selector ? 20 : 10) : 0;
    case 0x47:
        /* Go uses the current incoming byte for these two responses. */
        return pos == 2 && !byte ? 2 : pos == 4 && !byte ? 1 : 0;
    case 0x48:
        if (!pos) p->selector = byte;
        if (p->selector > 1) return 0;
        if (pos == 4) return 1;
        return pos == 5 ? (p->selector ? p->large : p->small) : 0;
    case 0x49:
        if (!pos) { p->selector = byte; p->write_enable = 0; }
        if (pos == 1 && p->selector < 2) p->write_enable = byte;
        if (pos == 2 && p->selector < 2 && p->write_enable == 1)
            pops_motor(p, p->selector, byte);
        return 0;
    case 0x4a:
        if (!pos) p->muted = byte != 0;
        return 0;
    case 0x4b: return 0;
    case 0x4c: return pos == 3 ? (s[0x24] == 0x73 ? 7 : 4) : 0;
    case 0x4d: {
        unsigned bit = 1u << pos;
        /* Match the inspected Go response: enabled slots return 1, even
         * for the small motor. Destination remains a separate mask. */
        int old = p->map_enable & bit ? 1 : 0xff;
        if (byte <= 1) {
            p->map_enable |= bit;
            if (byte) p->map_large |= bit; else p->map_large &= ~bit;
        } else p->map_enable &= ~bit;
        return old;
    }
    default: return 0x1ff;
    }
}
#endif

/* SPDX-License-Identifier: GPL-2.0-or-later
 * AC97 status DCH+CELV is authoritative at the last valid descriptor.
 * PICB may retain a count at halt; never infer a drained ring from time alone.
 */
static unsigned xbox_dma_remaining(unsigned remaining,unsigned status,unsigned control,
                                    unsigned current,unsigned last){
    return (status&3)==3&&(control&1)&&current==last?0:remaining;
}

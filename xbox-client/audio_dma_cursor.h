/* SPDX-License-Identifier: GPL-2.0-or-later
 * AC97 status DCH+CELV is authoritative at the last valid descriptor.
 * PICB may retain a count at halt; never infer a drained ring from time alone.
 */
static unsigned xbox_dma_remaining(unsigned remaining,unsigned status,unsigned control,
                                    unsigned current,unsigned last){
    return (status&3)==3&&(control&1)&&current==last?0:remaining;
}
/* A silent tail is not media: entering it proves all preceding samples played.
 * After halt, some controllers expose a prefetched CIV. Accept LVI only after
 * observing this DMA channel running, never merely because RUN was requested. */
static int xbox_dma_tail_reached(unsigned serial,unsigned last_serial,unsigned tail,
                                 unsigned status,unsigned control,int ran){
    return tail&&(serial==tail||(ran&&(status&1)&&(control&1)&&last_serial==tail));
}

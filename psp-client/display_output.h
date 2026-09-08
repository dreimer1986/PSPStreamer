/* Single owner of output mode/stride. No decoder or clock dependencies. */
#ifndef PSP_STREAMER_DISPLAY_OUTPUT_H
#define PSP_STREAMER_DISPLAY_OUTPUT_H

typedef struct {
    int tv;
    int (*mode)(int tv);
    int (*framebuffer)(int tv);
} DisplayOutput;

/* Re-entering TV for video must NOT perform another DVE mode transition.
 * Commit state only after both hardware operations succeed. On failure,
 * restore the previous mode and matching framebuffer as one pair. */
static int display_output_select(DisplayOutput *output, int tv) {
    int result, previous = output->tv;
    tv = !!tv;
    if (previous == tv) return 0;
    result = output->mode(tv);
    if (result >= 0) result = output->framebuffer(tv);
    if (result < 0) {
        output->mode(previous);
        output->framebuffer(previous);
        return result;
    }
    output->tv = tv;
    return 0;
}
#endif

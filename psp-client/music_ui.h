#ifndef PSPSTREAMER_MUSIC_UI_H
#define PSPSTREAMER_MUSIC_UI_H

/* Presentation policy only. Never use these intervals as playback clocks. */
#define MUSIC_UI_INTERVAL_US 50000ULL
#define MUSIC_UI_INPUT_POLL_US 10000
#define MUSIC_UI_THREAD_PRIORITY 0x40
/* Session-only opt-in. The default receiver remains the startup view. */
static int music_visual_active;

/* Keep the existing attack/decay exactly, including decay when target equals
 * displayed level. This envelope is visual, not an audio sample transform. */
static inline int music_ui_envelope(int displayed, int target) {
    return target > displayed ? displayed + (target - displayed + 1) / 2
                             : displayed > 3 ? displayed - 3 : 0;
}

/* Scope priority to the music caller. DAC/decoder workers are not modified. */
static inline int music_ui_lower_priority(void) {
    int priority = sceKernelGetThreadCurrentPriority();
    if (priority >= 0 && priority < MUSIC_UI_THREAD_PRIORITY &&
        sceKernelChangeThreadPriority(0, MUSIC_UI_THREAD_PRIORITY) >= 0)
        return priority;
    return -1;
}

static inline void music_ui_restore_priority(int priority) {
    if (priority >= 0) sceKernelChangeThreadPriority(0, priority);
}
#endif

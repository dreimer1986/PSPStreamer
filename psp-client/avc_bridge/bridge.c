#include <pspkernel.h>

/*
 * Minimal 6.61 bridge for the legacy mpeg_vsh codec module.
 *
 * The module is intentionally tiny: it has no hooks and does not patch any
 * system code.  Its only job is to expose the historical cooleyesBridge ABI
 * from kernel mode and invoke the 6.60+ Media Engine boot entry point.
 */
PSP_MODULE_INFO("cooleyesBridge", 0x1006, 1, 0);
PSP_MAIN_THREAD_ATTR(0);

extern int sceMeBootStart660(int mode);
extern int sceAudioSetFrequency660(int frequency);

int cooleyesMeBootStart(int devkit_version, int mode) {
    (void)devkit_version;
    return sceMeBootStart660(mode);
}

int cooleyesAudioSetFrequency(int devkit_version, int frequency) {
    (void)devkit_version;
    return sceAudioSetFrequency660(frequency);
}

int module_start(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    /* mpeg_vsh invokes the exported boot function too; booting here makes
     * loading deterministic and the firmware call is idempotent. */
    return cooleyesMeBootStart(sceKernelDevkitVersion(), 1);
}

int module_stop(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}

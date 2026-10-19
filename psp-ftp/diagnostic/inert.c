/* SPDX-License-Identifier: GPL-2.0-or-later
 * Load-only hardware diagnostic, NOT a working FTP plugin.
 * Same module name/path as the candidate, deliberately no runtime imports,
 * workers, buffers, hooks, filesystem or USB activity. */
#include <pspkernel.h>
PSP_MODULE_INFO("PSPStreamFTP",PSP_MODULE_KERNEL,0,7);
int module_start(SceSize size,void *args) {
    (void)size;(void)args;return 0;
}
int module_stop(SceSize size,void *args) {
    (void)size;(void)args;return 0;
}

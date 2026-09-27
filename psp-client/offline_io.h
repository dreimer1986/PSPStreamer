/* SPDX-License-Identifier: GPL-2.0-or-later
 * A submitted PSP async request owns its buffer until completion. Never cancel
 * by freeing memory or closing its descriptor under the I/O manager. */
static int offline_io_finish(int fd,long long *result,unsigned long long *wait_us) {
    unsigned long long start=sceKernelGetSystemTimeWide();
    int rc;
    while((rc=sceIoPollAsync(fd,result))==1)sceKernelDelayThread(1000);
    if(wait_us)*wait_us+=sceKernelGetSystemTimeWide()-start;
    return rc;
}

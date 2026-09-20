/* Cooperate with the optional OC worker BEFORE loadexec tears down threads.
 * Old/missing plugins simply reject the extension. Never block HOME forever. */
#include "../psp-overclock/control_api.h"
static int prepare_oc_exit(void) {
    int result=sceIoDevctl(OC_DEVICE,OC_CMD_PREPARE_EXIT,NULL,0,NULL,0);
    if(result<0)return result;
    for(int attempt=0;attempt<250;attempt++) {
        result=sceIoDevctl(OC_DEVICE,OC_CMD_EXIT_STATUS,NULL,0,NULL,0);
        if(result!=1)return result;
        sceKernelDelayThread(10000);
    }
    return -1;
}
/* Reap a cooperatively cancelled worker before unloading its dependencies.
 * On timeout, leave dependencies alive for process-wide exit cleanup. */
static int exit_join_worker(int (*stop)(void)) {
    for(int attempt=0;attempt<200;attempt++) {
        if(stop())return 1;
        sceKernelDelayThread(10000);
    }
    return 0;
}

/* Application-owned sockets only; resolver/firmware internal sockets are not
 * included. Capture errno before diagnostics can change thread-local errno. */
#include "tls_transport.h"
static volatile unsigned int socket_opens,socket_closes,socket_open_failures,socket_close_failures;
static volatile int socket_live,socket_peak;
static unsigned long long socket_snapshot_next;
static volatile int socket_snapshot_lock;
static void socket_snapshot(const char *event,int error) {
    if(!debug_enabled)return;
    SceNetMallocStat stat={0};
    int result=sceNetGetMallocStat(&stat);
    char info[192];
    snprintf(info,sizeof(info),"errno=%d hex=0x%08X open=%u close=%u live=%d peak=%d open_fail=%u close_fail=%u pool_rc=%d pool=%d max=%d free=%d",
        error,(unsigned int)error,socket_opens,socket_closes,socket_live,socket_peak,
        socket_open_failures,socket_close_failures,result,stat.pool,stat.maximum,stat.free);
    recovery_log(event,error,0,info);
}
static int socket_tracked_open(int domain,int type,int protocol) {
    int fd=sceNetInetSocket(domain,type,protocol);
    if(fd<0) {
        int error=sceNetInetGetErrno();
        __sync_fetch_and_add(&socket_open_failures,1);
        socket_snapshot("socket allocation failed",error);
    } else {
        __sync_fetch_and_add(&socket_opens,1);
        int live=__sync_add_and_fetch(&socket_live,1),peak;
        do {peak=socket_peak;if(live<=peak)break;} while(!__sync_bool_compare_and_swap(&socket_peak,peak,live));
    }
    return fd;
}
static int socket_tracked_close(int fd) {
    int result=tls_close(fd);
    if(result<0) {
        int error=sceNetInetGetErrno();
        __sync_fetch_and_add(&socket_close_failures,1);
        char detail[112];
        snprintf(detail,sizeof(detail),"fd=%d rc=0x%08X errno=%d errno_hex=0x%08X close=RST",
            fd,(unsigned int)result,error,(unsigned int)error);
        recovery_log("socket close result",result,0,detail);
        socket_snapshot("socket close failed",error);
    } else {
        __sync_fetch_and_add(&socket_closes,1);
        __sync_sub_and_fetch(&socket_live,1);
    }
    return result;
}
static void socket_snapshot_tick(void) {
    if(!debug_enabled || !socket_opens)return;
    if(__sync_lock_test_and_set(&socket_snapshot_lock,1))return;
    unsigned long long now=sceKernelGetSystemTimeWide();
    if(now>=socket_snapshot_next) {
        socket_snapshot_next=now+30000000ULL;
        socket_snapshot("socket resources",0);
    }
    __sync_lock_release(&socket_snapshot_lock);
}
/* All application socket creation in main.c and its included modules passes
 * here. tls_transport.c still owns the actual close and TLS destruction. */
#define sceNetInetSocket socket_tracked_open

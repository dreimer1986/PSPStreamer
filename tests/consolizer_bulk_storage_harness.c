/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../streammaster/protocol.h"
typedef int SceUID;
#define SM_CONTROLLER_PLUGIN 1
#define PSP_SMEM_High 1
static unsigned char arena[4*(SM_FRAME_SIZE+SM_BULK_MAX_FRAME_SIZE)+128];
static unsigned allocations,releases,bytes_requested;
static int fail,missing;
static int sceKernelAllocPartitionMemory(int p,const char *n,int t,unsigned bytes,void *address) {
    (void)n;assert(p==1 && t==PSP_SMEM_High && !address);
    allocations++;bytes_requested=bytes;return fail?-1:42;
}
static void *sceKernelGetBlockHeadAddr(int id){assert(id==42);return missing?NULL:arena+1;}
static int sceKernelFreePartitionMemory(int id){assert(id==42);releases++;return 0;}
#include "../psp-client/streammaster_usb/bulk_storage.h"
int main(void) {
    assert(!bulk_send && !bulk_recv && !allocations);
    fail=1;assert(bulk_storage_prepare()==SM_IO && !bulk_send && !bulk_recv);
    assert(!releases);bulk_storage_release();assert(!releases);
    fail=0;missing=1;assert(bulk_storage_prepare()==SM_IO && releases==1);
    missing=0;memset(arena,0xff,sizeof(arena));
    assert(bulk_storage_prepare()==0 && allocations==3);
    assert(bytes_requested==147456+63);
    assert(!((uintptr_t)bulk_send&63) && !((uintptr_t)bulk_recv&63));
    assert((uintptr_t)bulk_recv-(uintptr_t)bulk_send==4*SM_FRAME_SIZE);
    for(unsigned i=0;i<sizeof(SmResidentBulkStorage);i++)assert(((unsigned char *)bulk_send)[i]==0);
    assert(bulk_storage_prepare()==0 && allocations==3);
    for(unsigned i=0;i<4;i++){bulk_send[i].sequence=i;bulk_recv[i].payload[SM_BULK_MAX_FRAME_SIZE-33]=i+1;}
    bulk_storage_release();assert(!bulk_send && !bulk_recv && bulk_storage_id==-1 && releases==2);
    bulk_storage_release();assert(releases==2);
    assert(bulk_storage_prepare()==0 && allocations==4 && bulk_send[3].sequence==0);
    bulk_storage_release();assert(releases==3);
    return 0;
}

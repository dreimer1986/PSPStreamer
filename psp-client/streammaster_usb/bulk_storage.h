/* SPDX-License-Identifier: GPL-2.0-or-later
 * Games use controller EP0 and small PCM records, not download pipelines.
 * Keep the standalone application bridge's established static layout.
 * Resident allocation/release must hold the bridge lock; release ONLY after
 * all USB callbacks have relinquished their DMA buffers.
 */
#ifdef SM_CONTROLLER_PLUGIN
typedef struct {
    SmFrame send[SM_BULK_MAX_DEPTH];
    SmBulkFrame recv[SM_BULK_MAX_DEPTH];
} SmResidentBulkStorage;
static SceUID bulk_storage_id=-1;
static SmFrame *bulk_send;
static SmBulkFrame *bulk_recv;
static int bulk_storage_prepare(void) {
    if(bulk_storage_id>=0)return SM_OK;
    SceUID id=sceKernelAllocPartitionMemory(1,"Consolizer download",PSP_SMEM_High,sizeof(SmResidentBulkStorage)+63,NULL);
    if(id<0)return SM_IO;
    void *raw=sceKernelGetBlockHeadAddr(id);
    if(!raw){sceKernelFreePartitionMemory(id);return SM_IO;}
    SmResidentBulkStorage *storage=(void *)(((uintptr_t)raw+63)&~(uintptr_t)63);
    memset(storage,0,sizeof(*storage));
    bulk_send=storage->send;bulk_recv=storage->recv;bulk_storage_id=id;
    return SM_OK;
}
static void bulk_storage_release(void) {
    if(bulk_storage_id>=0)sceKernelFreePartitionMemory(bulk_storage_id);
    bulk_send=NULL;bulk_recv=NULL;bulk_storage_id=-1;
}
#else
static SmFrame bulk_send[SM_BULK_MAX_DEPTH] __attribute__((aligned(64)));
static SmBulkFrame bulk_recv[SM_BULK_MAX_DEPTH] __attribute__((aligned(64)));
static int bulk_storage_prepare(void){return SM_OK;}
static void bulk_storage_release(void){}
#endif

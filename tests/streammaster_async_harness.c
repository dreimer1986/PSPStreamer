/* Actual kernel begin/finish code with explicit simulated DMA completion. */
#include <assert.h>
#include <string.h>
#include "streammaster/protocol.h"
struct UsbEndpoint {int unused;};
struct UsbdDeviceReq {
    struct UsbEndpoint *endp;
    void *data;
    int size,recvsize,retcode;
    int (*func)(struct UsbdDeviceReq *,int,int);
};
static struct UsbEndpoint endpoints[3];
static struct UsbdDeviceReq send_req,recv_req;
static SmFrame send_frame,recv_frame;
static int started=1,attached=1,poisoned,cancelled,send_pending,recv_pending;
static int exchange_active,exchange_compact,event_id,events;
static SmFrame bulk_send[SM_BULK_MAX_DEPTH];
static SmBulkFrame bulk_recv[SM_BULK_MAX_DEPTH];
static struct UsbdDeviceReq bulk_send_req[SM_BULK_MAX_DEPTH],bulk_recv_req[SM_BULK_MAX_DEPTH];
static unsigned bulk_pending;
static int bulk_active,bulk_count=2;
static SmBulkDiag bulk_diag;
typedef unsigned SceUInt;
typedef unsigned u32;
#define PSP_EVENT_WAITOR 1
#define PSP_EVENT_WAITCLEAR 2
static int sceKernelWaitEventFlag(int id,unsigned mask,int mode,unsigned *bits,unsigned *timeout){
    (void)id;(void)mode;(void)timeout;
    *bits=events&mask;events&=~*bits;return *bits?0:-1;
}
static int done(struct UsbdDeviceReq *r,int a,int b){
    (void)a;(void)b;
    for(unsigned i=0;i<SM_BULK_MAX_DEPTH;i++) {
        unsigned bit=r==&bulk_send_req[i]?(8U<<i):r==&bulk_recv_req[i]?(128U<<i):0;
        if(bit){bulk_pending&=~bit;events|=bit;return 0;}
    }
    if(r==&send_req){send_pending=0;events|=1;}
    else {recv_pending=0;events|=2;}
    return 0;
}
static void cancel_requests(void){cancelled=1;events|=4;}
static void sceKernelClearEventFlag(int id,int bits){(void)id;events&=bits;}
static void sceKernelDcacheWritebackRange(void *p,unsigned n){(void)p;(void)n;}
static void sceKernelDcacheWritebackInvalidateRange(void *p,unsigned n){(void)p;(void)n;}
static void sceKernelDcacheInvalidateRange(void *p,unsigned n){(void)p;(void)n;}
static int sceUsbbdReqRecv(struct UsbdDeviceReq *r){assert(r==&recv_req || (r>=bulk_recv_req && r<bulk_recv_req+SM_BULK_MAX_DEPTH));return 0;}
static int sceUsbbdReqSend(struct UsbdDeviceReq *r){
    assert(r==&send_req || (r>=bulk_send_req && r<bulk_send_req+SM_BULK_MAX_DEPTH));
    if(r==&bulk_send_req[0])for(int i=0;i<bulk_count;i++)assert(bulk_pending&(128U<<i));
    return 0;
}
static int wait_request(struct UsbdDeviceReq *r,int bit){
    if(cancelled || !(events&bit))return SM_TIMEOUT;
    events&=~bit;
    return r->retcode?SM_IO:0;
}
/* EXCHANGE */
static void complete(int wrong_sequence){
    recv_frame=send_frame;recv_frame.flags=SM_REPLY;
    recv_frame.sequence+=wrong_sequence;
    recv_frame.length=4;memcpy(recv_frame.payload,"data",4);sm_seal(&recv_frame);
    send_req.recvsize=send_req.size;recv_req.recvsize=sm_wire_size(4);
    done(&recv_req,0,0);done(&send_req,0,0);
}
static void complete_bulk(int i,unsigned length,int result){
    SmBulkFrame *f=&bulk_recv[i];memset(f,0,sizeof(*f));
    f->magic=SM_MAGIC;f->version=SM_VERSION;f->op=bulk_send[i].op;f->flags=SM_REPLY;
    f->sequence=bulk_send[i].sequence;f->result=result;f->length=length;
    memset(f->payload,65+i,length);f->checksum=sm_bulk_checksum(f);
    bulk_send_req[i].recvsize=bulk_send_req[i].size;
    bulk_recv_req[i].recvsize=sm_bulk_wire_size_op(f->op,length);
    done(&bulk_recv_req[i],0,0);done(&bulk_send_req[i],0,0);
}
int main(void){
    SmFrame request={.op=SM_SOCKET_READ,.sequence=7,.length=sizeof(SmSocketRequest)},reply;
    SmSocketRequest read={42,SM_PAYLOAD_SIZE};memcpy(request.payload,&read,sizeof(read));sm_seal(&request);
    assert(exchange_begin(&request,1)==0 && exchange_active && send_pending && recv_pending);
    assert(exchange_begin(&request,1)==SM_BUSY);
    /* Caller is free to reuse its request immediately after begin. */
    memset(&request,0,sizeof(request));assert(send_frame.sequence==7);
    complete(0);
    /* Even completed DMA owns its buffers until finish consumes the reply. */
    assert(exchange_begin(&request,1)==SM_BUSY);
    assert(exchange_finish(&reply)==0 && !exchange_active && !memcmp(reply.payload,"data",4));
    assert(exchange_finish(&reply)==SM_INVALID);
    request=send_frame;request.flags=0;sm_seal(&request);
    assert(exchange_begin(&request,1)==0);complete(1);
    assert(exchange_finish(&reply)==SM_IO && poisoned);
    assert(exchange_begin(&request,1)==SM_BUSY);
    poisoned=0;
    assert(exchange_begin(&request,1)==0);cancel_requests();
    assert(exchange_finish(&reply)==SM_TIMEOUT && poisoned && send_pending && recv_pending);
    /* Cancellation never reuses buffers still owned by DMA. */
    assert(exchange_begin(&request,1)==SM_BUSY);
    complete(0);assert(!send_pending && !recv_pending);
    poisoned=0;cancelled=0;
    request.op=SM_SOCKET_READ_BULK;request.sequence=123;sm_seal(&request);
    SmBulkResult pair;
    assert(bulk_begin(&request)==0 && bulk_pending==(8|16|128|256));
    assert(bulk_begin(&request)==SM_BUSY);
    /* Callback order is independent of request order; data remains FIFO. */
    complete_bulk(1,SM_BULK_PAYLOAD_SIZE,0);complete_bulk(0,SM_BULK_PAYLOAD_SIZE,0);
    assert(bulk_finish(&pair)==0 && pair.length==SM_PAIR_PAYLOAD_SIZE);
    for(unsigned i=0;i<pair.length;i++)assert(pair.payload[i]==(i<SM_BULK_PAYLOAD_SIZE?'A':'B'));
    assert(bulk_begin(&request)==0);complete_bulk(0,0,SM_BUSY);complete_bulk(1,32,0);
    assert(bulk_finish(&pair)==0 && !pair.result && pair.length==32 && pair.payload[0]=='B');
    assert(bulk_begin(&request)==0);complete_bulk(0,0,0);complete_bulk(1,0,0);
    assert(bulk_finish(&pair)==0 && !pair.length && !pair.result);
    assert(bulk_begin(&request)==0);complete_bulk(0,4,0);complete_bulk(1,4,0);
    bulk_recv[1].payload[0]^=1;
    assert(bulk_finish(&pair)==SM_IO && poisoned);
    assert(bulk_diag.result==SM_IO && bulk_diag.index==1 && bulk_diag.receive==1 && !bulk_diag.pending);
    poisoned=0;assert(bulk_begin(&request)==0);cancel_requests();
    assert(bulk_finish(&pair)==SM_TIMEOUT && bulk_pending && poisoned);
    assert(bulk_diag.result==SM_TIMEOUT && bulk_diag.index==0 && !bulk_diag.receive && bulk_diag.pending==bulk_pending);
    assert(bulk_begin(&request)==SM_BUSY);
    complete_bulk(0,0,0);complete_bulk(1,0,0);assert(!bulk_pending);
    poisoned=0;
    for(unsigned kib=8;kib<=32;kib*=2)for(unsigned depth=1;depth<=4;depth*=2) {
        unsigned payload=kib*1024-32;
        SmSocketRequest read={1,payload};memcpy(request.payload,&read,sizeof(read));
        request.op=SM_SOCKET_READ_BULK_EXT;request.result=depth;sm_seal(&request);
        if(payload*depth>SM_MAX_GROUP_PAYLOAD){assert(bulk_begin(&request)==SM_INVALID);continue;}
        assert(bulk_begin(&request)==0);
        for(int i=(int)depth-1;i>=0;i--){
            unsigned expected=(sm_bulk_wire_size_op(request.op,payload)+63U)&~63U;
            assert((unsigned)bulk_recv_req[i].size==expected);
            assert(expected<=SM_BULK_MAX_FRAME_SIZE);
            complete_bulk(i,payload,0);
        }
        assert(!bulk_finish(&pair) && pair.length==payload*depth && !bulk_pending);
        for(unsigned i=0;i<pair.length;i++)assert(pair.payload[i]==65+i/payload);
        /* Empty first replies are common while the HTTP body starts. Later
         * replies in the same group must retain their data and FIFO order. */
        assert(!bulk_begin(&request));
        for(unsigned i=0;i<depth;i++)complete_bulk(i,i==depth-1?17:0,i==depth-1?0:SM_BUSY);
        assert(!bulk_finish(&pair) && pair.length==17 && !pair.result);
        assert(pair.payload[0]==65+depth-1);
    }
}

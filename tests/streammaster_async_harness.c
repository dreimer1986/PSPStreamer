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
static int done(struct UsbdDeviceReq *r,int a,int b){
    (void)a;(void)b;
    if(r==&send_req){send_pending=0;events|=1;}
    else {recv_pending=0;events|=2;}
    return 0;
}
static void cancel_requests(void){cancelled=1;events|=4;}
static void sceKernelClearEventFlag(int id,int bits){(void)id;events&=bits;}
static void sceKernelDcacheWritebackRange(void *p,unsigned n){(void)p;(void)n;}
static void sceKernelDcacheWritebackInvalidateRange(void *p,unsigned n){(void)p;(void)n;}
static void sceKernelDcacheInvalidateRange(void *p,unsigned n){(void)p;(void)n;}
static int sceUsbbdReqRecv(struct UsbdDeviceReq *r){assert(r==&recv_req);return 0;}
static int sceUsbbdReqSend(struct UsbdDeviceReq *r){assert(r==&send_req);return 0;}
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
}

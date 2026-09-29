/* Host ownership test for the actual ESP reply pool and network worker. */
#include <assert.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <setjmp.h>
#include "streammaster/protocol.h"
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
/* TYPES */
typedef struct {unsigned head,count,capacity,size;unsigned char data[4*sizeof(Work)];} Queue;
static Queue cq,rq,fq;
static Queue *commands=&cq,*replies=&rq,*free_replies=&fq;
static Reply reply_pool[2];
static atomic_uint epoch;
static int client,cancel_free,alloc_fail,allocations,consumed;
static unsigned last_allocation;
static jmp_buf worker_idle;
static void *heap_caps_malloc(unsigned size,unsigned caps) {
    assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(alloc_fail)return NULL;
    allocations++;last_allocation=size;return malloc(size);
}
static void heap_caps_free(void *p){free(p);}
static int xQueueSend(Queue *q,const void *value,int wait) {
    (void)wait;if(q->count==q->capacity)return 0;
    memcpy(q->data+((q->head+q->count)%q->capacity)*q->size,value,q->size);q->count++;return 1;
}
static int xQueueReceive(Queue *q,void *value,int wait) {
    (void)wait;
    if(q==free_replies && cancel_free){cancel_free=0;epoch++;return 0;}
    if(!q->count){if(q==commands)longjmp(worker_idle,1);return 0;}
    memcpy(value,q->data+q->head*q->size,q->size);q->head=(q->head+1)%q->capacity;q->count--;return 1;
}
static void usb_host_client_unblock(int c){(void)c;}
static int64_t esp_timer_get_time(void){static int64_t t;return ++t;}
static void sm_network_idle(void){}
static int process_work(Work *work,Work *response) {
    if(work->epoch!=epoch)return 0;
    response->frame=work->frame;response->frame.flags=SM_REPLY;sm_seal(&response->frame);return 1;
}
static void sm_sockets_bulk_read(const SmFrame *request,SmBulkFrame *out) {
    SmSocketRequest r;memcpy(&r,request->payload,sizeof(r));
    unsigned limit=request->op==SM_SOCKET_READ_BULK?SM_BULK_PAYLOAD_SIZE:SM_BULK_MAX_FRAME_SIZE-32;
    if(r.length>limit)r.length=limit;
    memset(out,0,32);out->magic=SM_MAGIC;out->version=SM_VERSION;
    out->op=request->op;out->sequence=request->sequence;out->flags=SM_REPLY;out->length=r.length;
    memset(out->payload,request->sequence&255,r.length);consumed++;
    if(sm_bulk_wire_size_op(out->op,r.length)>32+r.length)out->payload[r.length]=0;
    out->checksum=sm_bulk_checksum(out);
}
static void sm_sockets_bulk_cost(uint32_t *copy,uint32_t *checksum){*copy=1;*checksum=2;}
/* POOL */
/* WORKER */
static void enqueue(unsigned op,unsigned payload,unsigned seq) {
    Work w={.epoch=epoch};w.frame.op=op;w.frame.sequence=seq;w.frame.length=sizeof(SmSocketRequest);
    SmSocketRequest r={1,payload};memcpy(w.frame.payload,&r,sizeof(r));sm_seal(&w.frame);
    assert(xQueueSend(commands,&w,0));
}
static void run(void){if(!setjmp(worker_idle))network_worker(NULL);}
static Reply *take(void){Reply *p=NULL;assert(xQueueReceive(replies,&p,0));return p;}
int main(void) {
    cq.capacity=4;cq.size=sizeof(Work);rq.capacity=fq.capacity=2;rq.size=fq.size=sizeof(Reply *);
    reply_release(&reply_pool[0]);reply_release(&reply_pool[1]);
    for(unsigned cycle=0;cycle<1000;cycle++) {
        enqueue(SM_SOCKET_READ_BULK,8160,1);enqueue(SM_SOCKET_READ_BULK,8160,2);run();
        assert(!fq.count && rq.count==2 && !allocations);
        Reply *a=take(),*b=take();assert(a!=b && a->packet==a->local && b->packet==b->local);
        assert(sm_bulk_valid((SmBulkFrame *)a->packet));assert(sm_bulk_valid((SmBulkFrame *)b->packet));
        assert(a->packet[32]==1 && b->packet[32]==2);
        reply_release(a); /* DMA must hold its independent copy before release. */
        enqueue(SM_SOCKET_READ_BULK,8160,3);run();assert(b->packet[32]==2);
        reply_release(b);discard_replies();assert(fq.count==2);
    }
    enqueue(SM_SOCKET_READ_BULK_EXT,8160,4);run();Reply *p=take();
    assert(p->packet==p->local && p->wire_size==8193 && !allocations);reply_release(p);
    for(unsigned size=16352;size<=32736;size+=16384) {
        enqueue(SM_SOCKET_READ_BULK_EXT,size,5);run();p=take();
        assert(p->packet==p->extended && sm_bulk_valid((SmBulkFrame *)p->packet));
        assert(last_allocation==sm_bulk_wire_size_op(SM_SOCKET_READ_BULK_EXT,size));reply_release(p);
    }
    enqueue(SM_SOCKET_READ_BULK,8160,6);run();p=take();assert(p->packet==p->local);reply_release(p);
    /* Detach cannot reclaim a worker-held slot or lose a queued pointer. */
    Reply *held=NULL;assert(xQueueReceive(free_replies,&held,0));
    enqueue(SM_SOCKET_READ_BULK,8160,7);run();epoch++;discard_replies();assert(fq.count==1);
    reply_release(held);assert(fq.count==2);
    cancel_free=1;enqueue(SM_SOCKET_READ_BULK,8160,8);run();assert(fq.count==2 && !rq.count);
    /* Failed optional allocation returns a checked error, consuming no data. */
    for(unsigned i=0;i<2;i++) {
        free(reply_pool[i].extended);reply_pool[i].extended=NULL;reply_pool[i].extended_capacity=0;
    }
    int before=consumed;alloc_fail=1;enqueue(SM_SOCKET_READ_BULK_EXT,32736,9);run();p=take();
    assert(consumed==before && p->packet==p->local && p->wire_size==sm_wire_size(0));
    assert(sm_bulk_valid((SmBulkFrame *)p->packet) && ((SmBulkFrame *)p->packet)->result==SM_IO);
    reply_release(p);assert(fq.count==2);
}

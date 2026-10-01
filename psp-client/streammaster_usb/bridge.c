/* SPDX-License-Identifier: GPL-2.0-or-later
 * PSP USB device driver; padded SDK descriptors follow the documented
 * PSPLINK/PSPSDK layout (James F / pspdev, BSD). No global network hooks. */
#include <pspkernel.h>
#include <pspsdk.h>
#include <pspusb.h>
#include <pspusbbus.h>
#include <pspiofilemgr_kernel.h>
#include <string.h>
#include "../../streammaster/protocol.h"
PSP_MODULE_INFO("StreamMasterUSB",PSP_MODULE_KERNEL,0,1);
#define DRIVER "StreamMasterUSBDriver"
static struct UsbEndpoint endpoints[3]={{0,0,0},{1,0,0},{2,0,0}};
static struct UsbInterface interface={0xffffffff,0,1};
static struct UsbData descriptors[2] __attribute__((aligned(64)));
static struct UsbdDeviceReq send_req,recv_req;
static SmPad pad_value;
static volatile int pad_enabled;
static unsigned long long pad_time;
static SmFrame send_frame __attribute__((aligned(64))),recv_frame __attribute__((aligned(64)));
static SceUID event_id=-1,lock_id=-1;
static volatile int attached,send_pending,recv_pending,poisoned,cancelled;
static int started;
static int exchange_active,exchange_compact;
static SmFrame bulk_send[SM_BULK_MAX_DEPTH] __attribute__((aligned(64)));
static SmBulkFrame bulk_recv[SM_BULK_MAX_DEPTH] __attribute__((aligned(64)));
static struct UsbdDeviceReq bulk_send_req[SM_BULK_MAX_DEPTH],bulk_recv_req[SM_BULK_MAX_DEPTH];
static volatile unsigned bulk_pending;
static int bulk_active,bulk_count=2;
static SmBulkDiag bulk_diag;
static struct UsbDriver driver;
static unsigned char usb_string[]={26,3,'S',0,'t',0,'r',0,'e',0,'a',0,'m',0,'M',0,'a',0,'s',0,'t',0,'e',0,'r',0};
static int control_request(int a,int b,struct DeviceRequest *r) {
    (void)a;(void)b;
    /* Vendor DEVICE request with no data stage. Reuses EP0: no ninth USB
     * host channel and no interaction with the media request/reply queues. */
    if(r && r->bmRequestType==0x40 && (r->bRequest==0x53 || r->bRequest==0x54) && !r->wLength && !(r->wValue&~0xf3f9U)) {
        int intr=sceKernelCpuSuspendIntr();
        pad_value=(SmPad){.magic=SM_PAD_MAGIC,.buttons=r->wValue,.connected=r->bRequest==0x53,.x=r->wIndex&255,.y=r->wIndex>>8};
        pad_time=sceKernelGetSystemTimeWide();sceKernelCpuResumeIntr(intr);
    }
    return 0;
}
static int control_complete(int a,int b,int c){(void)a;(void)b;(void)c;return 0;}
static int done(struct UsbdDeviceReq *r,int a,int b) {
    (void)a;(void)b;
    for(unsigned i=0;i<SM_BULK_MAX_DEPTH;i++) {
        unsigned bit=r==&bulk_send_req[i]?(8U<<i):r==&bulk_recv_req[i]?(128U<<i):0;
        if(bit){__sync_fetch_and_and(&bulk_pending,~bit);sceKernelSetEventFlag(event_id,bit);return 0;}
    }
    if(r==&send_req)send_pending=0;else recv_pending=0;
    sceKernelSetEventFlag(event_id,r==&send_req?1:2);return 0;
}
static void cancel_requests(void) {
    cancelled=1;
    if(started){sceUsbbdReqCancelAll(&endpoints[1]);sceUsbbdReqCancelAll(&endpoints[2]);}
    sceKernelSetEventFlag(event_id,4);
}
static int attach(int speed,void *a,void *b){(void)a;(void)b;attached=speed;pad_enabled=1;pad_time=0;return 0;}
static void pad_cancel(void){pad_enabled=0;pad_time=0;}
static int detach(int a,int b,int c){(void)a;(void)b;(void)c;attached=0;pad_cancel();cancel_requests();return 0;}
static int start_driver(int size,void *args) {
    (void)size;(void)args;memset(descriptors,0,sizeof(descriptors));
    struct DeviceDescriptor device={18,1,0x0200,0,0,0,64,0,0,0x0101,0,0,0,1};
    struct ConfigDescriptor config={9,2,32,1,1,0,0xc0,0};
    struct InterfaceDescriptor face={9,4,0,0,2,0xff,SM_USB_SUBCLASS,SM_USB_PROTOCOL,1};
    for(int i=0;i<2;i++) {
        struct UsbData *d=&descriptors[i];
        memcpy(d->devdesc,&device,sizeof(device));memcpy(d->confdesc.desc,&config,sizeof(config));
        memcpy(d->interdesc.desc,&face,sizeof(face));
        d->config.pconfdesc=&d->confdesc;d->config.pinterfaces=&d->interfaces;
        d->config.pinterdesc=&d->interdesc;d->config.pendp=d->endp;
        d->confdesc.pinterfaces=&d->interfaces;d->interfaces.pinterdesc[0]=&d->interdesc;
        d->interfaces.intcount=1;d->interdesc.pendp=d->endp;
        for(int j=0;j<2;j++){struct EndpointDescriptor ep={7,5,j?0x02:0x81,2,i?64:512,0};memcpy(d->endp[j].desc,&ep,sizeof(ep));}
    }
    driver.devp_hi=descriptors[0].devdesc;driver.confp_hi=&descriptors[0].config;
    driver.devp=descriptors[1].devdesc;driver.confp=&descriptors[1].config;
    return 0;
}
static int stop_driver(int size,void *args){(void)size;(void)args;attached=0;pad_cancel();cancel_requests();return 0;}
static struct UsbDriver driver={.name=DRIVER,.endpoints=3,.endp=endpoints,.intp=&interface,
    .str=(struct StringDescriptor *)usb_string,.recvctl=control_request,.func28=control_complete,
    .attach=attach,.detach=detach,.start_func=start_driver,.stop_func=stop_driver};
static int user_buffer(void *p,int bytes) {
    uintptr_t a=(uintptr_t)p;
    return bytes>=0 && a>=0x08800000U && a<0x0c000000U && (unsigned)bytes<=0x0c000000U-a;
}
static int wait_request(struct UsbdDeviceReq *request,int bit) {
    SceUInt timeout=(send_frame.op>=SM_SOCKET_OPEN || send_frame.op==SM_USB_METRICS)?500000:15000000;u32 bits=0;
    int rc=sceKernelWaitEventFlag(event_id,bit|4,PSP_EVENT_WAITOR|PSP_EVENT_WAITCLEAR,&bits,&timeout);
    if(rc<0 || (bits&4) || cancelled)return SM_TIMEOUT;
    if(request->retcode || request->recvsize<32 || request->recvsize>request->size)return SM_IO;
    if(bit==1 && request->recvsize!=request->size)return SM_IO;
    return SM_OK;
}
static int shutdown_usb(void) {
    pad_cancel();
    cancel_requests();
    if(started)sceUsbDeactivate(SM_USB_PID);
    for(int i=0;i<100 && (send_pending||recv_pending||bulk_pending);i++)sceKernelDelayThread(1000);
    if(send_pending || recv_pending || bulk_pending){poisoned=1;return SM_BUSY;}
    if(started){sceUsbStop(DRIVER,0,NULL);sceUsbStop(PSP_USBBUS_DRIVERNAME,0,NULL);started=0;}
    memset(&send_frame,0,sizeof(send_frame));memset(&recv_frame,0,sizeof(recv_frame));
    attached=0;poisoned=0;exchange_active=bulk_active=0;return 0;
}
static int exchange_begin(const void *in,int compact) {
    if(!started || !attached)return SM_OFFLINE;
    if(poisoned || exchange_active || bulk_active || bulk_pending || send_pending || recv_pending)return SM_BUSY;
    cancelled=0;memcpy(&send_frame,in,sizeof(send_frame));
    if(!sm_valid(&send_frame) || send_frame.flags)return SM_INVALID;
    if(compact){send_frame.flags=SM_COMPACT;sm_seal(&send_frame);}
    memset(&recv_frame,0,sizeof(recv_frame));memset(&send_req,0,sizeof(send_req));memset(&recv_req,0,sizeof(recv_req));
    send_req.endp=&endpoints[1];send_req.data=&send_frame;send_req.size=compact?sm_wire_size(send_frame.length):SM_FRAME_SIZE;send_req.func=done;
    recv_req.endp=&endpoints[2];recv_req.data=&recv_frame;recv_req.size=SM_FRAME_SIZE;recv_req.func=done;
    sceKernelClearEventFlag(event_id,0);
    sceKernelDcacheWritebackRange(&send_frame,sizeof(send_frame));
    sceKernelDcacheWritebackInvalidateRange(&recv_frame,sizeof(recv_frame));
    recv_pending=1;
    int rc=sceUsbbdReqRecv(&recv_req);if(rc<0){recv_pending=0;return rc;}
    send_pending=1;rc=sceUsbbdReqSend(&send_req);
    if(rc<0)send_pending=0;
    if(rc<0){poisoned=1;cancel_requests();return rc;}
    exchange_active=1;exchange_compact=compact;return 0;
}
static int exchange_finish(void *out) {
    if(!exchange_active)return SM_INVALID;
    int rc=wait_request(&send_req,1);
    if(rc>=0)rc=wait_request(&recv_req,2);
    if(rc>=0) {
        sceKernelDcacheInvalidateRange(&recv_frame,sizeof(recv_frame));
        if(!sm_valid(&recv_frame)||recv_frame.flags!=SM_REPLY || recv_frame.sequence!=send_frame.sequence || recv_frame.op!=send_frame.op ||
           recv_req.recvsize!=(int)(exchange_compact?sm_wire_size(recv_frame.length):SM_FRAME_SIZE))rc=SM_IO;
        else memcpy(out,&recv_frame,32+recv_frame.length);
    }
    if(rc<0){poisoned=1;cancel_requests();}
    exchange_active=0;
    return rc;
}
static int bulk_begin(const SmFrame *in) {
    if(!started || !attached)return SM_OFFLINE;
    if(poisoned || exchange_active || bulk_active || bulk_pending || send_pending || recv_pending)return SM_BUSY;
    memcpy(&bulk_send[0],in,32+sizeof(SmSocketRequest));
    if(bulk_send[0].length!=sizeof(SmSocketRequest) || (bulk_send[0].op!=SM_SOCKET_READ_BULK && bulk_send[0].op!=SM_SOCKET_READ_BULK_EXT) ||
       bulk_send[0].flags || !sm_valid(&bulk_send[0]))return SM_INVALID;
    int extended=bulk_send[0].op==SM_SOCKET_READ_BULK_EXT;
    bulk_count=extended?bulk_send[0].result:2;
    SmSocketRequest request;memcpy(&request,bulk_send[0].payload,sizeof(request));
    if((bulk_count!=1 && bulk_count!=2 && bulk_count!=4) || !request.length ||
       request.length>(extended?SM_BULK_MAX_FRAME_SIZE-32:SM_BULK_PAYLOAD_SIZE) ||
       request.length*bulk_count>SM_MAX_GROUP_PAYLOAD)return SM_INVALID;
    bulk_send[0].result=0;
    for(int i=1;i<bulk_count;i++) {
        memcpy(&bulk_send[i],&bulk_send[0],32+sizeof(SmSocketRequest));bulk_send[i].sequence+=i;
    }
    cancelled=0;bulk_active=1;memset(&bulk_diag,0,sizeof(bulk_diag));sceKernelClearEventFlag(event_id,0);
    for(int i=0;i<bulk_count;i++) {
        if(cancelled || !attached){poisoned=1;cancel_requests();return SM_TIMEOUT;}
        bulk_send[i].flags=SM_COMPACT;sm_seal(&bulk_send[i]);
        memset(&bulk_send_req[i],0,sizeof(bulk_send_req[i]));
        memset(&bulk_recv_req[i],0,sizeof(bulk_recv_req[i]));
        struct UsbdDeviceReq *s=&bulk_send_req[i],*r=&bulk_recv_req[i];
        s->endp=&endpoints[1];s->data=&bulk_send[i];s->size=sm_wire_size(sizeof(SmSocketRequest));s->func=done;
        /* Bound DMA/cache work to the negotiated payload, including the
         * extended framing's short-packet terminator and USB packet rounding. */
        r->endp=&endpoints[2];r->data=&bulk_recv[i];
        r->size=extended?(sm_bulk_wire_size_op(bulk_send[0].op,request.length)+63U)&~63U:SM_BULK_FRAME_SIZE;
        r->func=done;
        sceKernelDcacheWritebackRange(&bulk_send[i],s->size);
        sceKernelDcacheWritebackInvalidateRange(&bulk_recv[i],r->size);
        __sync_fetch_and_or(&bulk_pending,128U<<i);
        int rc=sceUsbbdReqRecv(r);
        if(rc<0){__sync_fetch_and_and(&bulk_pending,~(128U<<i));poisoned=1;cancel_requests();return rc;}
    }
    /* A short/empty reply can arrive immediately. Post the whole receive
     * window before allowing the host to process any request in this group. */
    for(int i=0;i<bulk_count;i++) {
        if(cancelled || !attached){poisoned=1;cancel_requests();return SM_TIMEOUT;}
        __sync_fetch_and_or(&bulk_pending,8U<<i);int rc=sceUsbbdReqSend(&bulk_send_req[i]);
        if(rc<0){__sync_fetch_and_and(&bulk_pending,~(8U<<i));poisoned=1;cancel_requests();return rc;}
    }
    return 0;
}
static int bulk_wait(struct UsbdDeviceReq *r,unsigned bit) {
    SceUInt timeout=500000;u32 bits=0;
    int rc=sceKernelWaitEventFlag(event_id,bit|4,PSP_EVENT_WAITOR|PSP_EVENT_WAITCLEAR,&bits,&timeout);
    return rc<0 || (bits&4) || cancelled?SM_TIMEOUT:r->retcode?SM_IO:0;
}
static int bulk_finish(SmBulkResult *out) {
    if(!bulk_active)return SM_INVALID;
    int rc=0;out->length=0;out->result=0;
    for(int i=0;i<bulk_count && rc>=0;i++) {
        bulk_diag.index=i;bulk_diag.receive=0;
        rc=bulk_wait(&bulk_send_req[i],8U<<i);
        if(rc>=0){bulk_diag.receive=1;rc=bulk_wait(&bulk_recv_req[i],128U<<i);}
        if(rc<0)break;
        SmBulkFrame *f=&bulk_recv[i];
        sceKernelDcacheInvalidateRange(f,bulk_recv_req[i].size);
        if(bulk_send_req[i].recvsize!=bulk_send_req[i].size || bulk_recv_req[i].recvsize<32 ||
           !sm_bulk_valid(f) || f->op!=bulk_send[i].op || f->sequence!=bulk_send[i].sequence ||
           bulk_recv_req[i].recvsize!=(int)sm_bulk_wire_size_op(f->op,f->length) || (f->result<0 && f->length) ||
           f->length>SM_MAX_GROUP_PAYLOAD-out->length) {rc=SM_IO;break;}
        if(f->length){memcpy(out->payload+out->length,f->payload,f->length);out->length+=f->length;}
        else if(f->result<0)out->result=f->result;
    }
    if(rc<0){
        bulk_diag.result=rc;bulk_diag.pending=bulk_pending;bulk_diag.count=bulk_count;
        for(int i=0;i<bulk_count;i++) {
            bulk_diag.item[i].sent=bulk_send_req[i].recvsize;
            bulk_diag.item[i].received=bulk_recv_req[i].recvsize;
            bulk_diag.item[i].send_rc=bulk_send_req[i].retcode;
            bulk_diag.item[i].recv_rc=bulk_recv_req[i].retcode;
        }
        poisoned=1;cancel_requests();
    }
    /* Buffered bytes precede EOF/errors; the next read observes terminal state. */
    if(out->length)out->result=0;
    bulk_active=0;return rc;
}
static int devctl(PspIoDrvFileArg *a,const char *name,unsigned cmd,void *in,int inlen,void *out,int outlen) {
    (void)a;(void)name;
    if(cmd==SM_DEV_BULK_DIAG) {
        if(inlen || outlen!=sizeof(bulk_diag) || !user_buffer(out,outlen))return SM_INVALID;
        SceUInt wait=100000;
        if(sceKernelWaitSema(lock_id,1,&wait)<0)return SM_BUSY;
        memcpy(out,&bulk_diag,sizeof(bulk_diag));sceKernelSignalSema(lock_id,1);return 0;
    }
    if(cmd==SM_DEV_GAMEPAD) {
        if(inlen || outlen!=sizeof(SmPad) || !user_buffer(out,outlen))return SM_INVALID;
        SmPad value={.magic=SM_PAD_MAGIC,.x=128,.y=128};
        int intr=sceKernelCpuSuspendIntr();
        if(pad_enabled && pad_time && sceKernelGetSystemTimeWide()-pad_time<750000)value=pad_value;
        sceKernelCpuResumeIntr(intr);
        memcpy(out,&value,sizeof(value));
        return 0;
    }
    if(cmd==SM_DEV_CANCEL){cancel_requests();return 0;}
    if(cmd==SM_DEV_STATUS)return attached?attached:poisoned?SM_BUSY:0;
    if(cmd==SM_DEV_BULK_CAPS)return 1;
    if(cmd==SM_DEV_BULK_EXT_CAPS)return 1;
    if((cmd==SM_DEV_EXCHANGE || cmd==SM_DEV_EXCHANGE_COMPACT) && (inlen!=SM_FRAME_SIZE || outlen!=SM_FRAME_SIZE || !user_buffer(in,inlen)||!user_buffer(out,outlen)))return SM_INVALID;
    if(cmd==SM_DEV_READ_BEGIN && (inlen!=SM_FRAME_SIZE || outlen || !user_buffer(in,inlen)))return SM_INVALID;
    if(cmd==SM_DEV_READ_FINISH && (inlen || outlen!=SM_FRAME_SIZE || !user_buffer(out,outlen)))return SM_INVALID;
    if(cmd==SM_DEV_BULK_BEGIN && (inlen!=SM_FRAME_SIZE || outlen || !user_buffer(in,inlen)))return SM_INVALID;
    if(cmd==SM_DEV_BULK_FINISH && (inlen ||
       outlen!=(int)(bulk_send[0].op==SM_SOCKET_READ_BULK_EXT?sizeof(SmBulkResult):SM_LEGACY_RESULT_SIZE) || !user_buffer(out,outlen)))return SM_INVALID;
    SceUInt timeout=100000;int rc=sceKernelWaitSema(lock_id,1,&timeout);if(rc<0)return SM_BUSY;
    if(cmd==SM_DEV_START) {
        if(started)rc=0;
        else if(sceUsbGetState()&PSP_USB_ACTIVATED)rc=SM_BUSY;
        else {
            rc=sceUsbStart(PSP_USBBUS_DRIVERNAME,0,NULL);
            if(rc>=0) {
                rc=sceUsbStart(DRIVER,0,NULL);
                if(rc>=0){started=1;cancelled=poisoned=0;rc=sceUsbActivate(SM_USB_PID);if(rc<0)shutdown_usb();}
                else sceUsbStop(PSP_USBBUS_DRIVERNAME,0,NULL);
            }
        }
    } else if(cmd==SM_DEV_STOP)rc=shutdown_usb();
    else if(cmd==SM_DEV_BULK_BEGIN)rc=bulk_begin(in);
    else if(cmd==SM_DEV_BULK_FINISH)rc=bulk_finish(out);
    else if(cmd==SM_DEV_READ_BEGIN) {
        /* Only socket reads may be submitted speculatively, exactly once. Never
         * retain a user pointer: begin copies into the static DMA buffer. */
        SmFrame *f=in;
        if(f->op!=SM_SOCKET_READ || f->length!=sizeof(SmSocketRequest))rc=SM_INVALID;
        else rc=exchange_begin(in,1);
    } else if(cmd==SM_DEV_READ_FINISH)rc=exchange_finish(out);
    else if(cmd==SM_DEV_EXCHANGE || cmd==SM_DEV_EXCHANGE_COMPACT) {
        rc=exchange_begin(in,cmd==SM_DEV_EXCHANGE_COMPACT);
        if(rc>=0)rc=exchange_finish(out);
    }
    else rc=SM_INVALID;
    sceKernelSignalSema(lock_id,1);return rc;
}
/* I/O manager requires both lifecycle callbacks even for a devctl-only
 * device. It must never retain a driver table after its PRX is unloaded. */
static int io_init(PspIoDrvArg *arg){(void)arg;return 0;}
static int io_exit(PspIoDrvArg *arg){(void)arg;return 0;}
static PspIoDrvFuncs io_functions={.IoInit=io_init,.IoExit=io_exit,.IoDevctl=devctl};
static PspIoDrv io_driver={"stm",0x10,0x800,"StreamMaster",&io_functions};
int module_start(SceSize size,void *args) {
    (void)size;(void)args;
    event_id=sceKernelCreateEventFlag("stm-events",0x200,0,NULL);
    lock_id=sceKernelCreateSema("stm-lock",0,1,1,NULL);
    int rc=event_id<0?event_id:lock_id;
    if(event_id<0 || lock_id<0)goto fail;
    rc=sceUsbbdRegister(&driver);if(rc<0)goto fail;
    rc=sceIoAddDrv(&io_driver);if(rc>=0)return 0;
    sceUsbbdUnregister(&driver);
fail:
    if(lock_id>=0)sceKernelDeleteSema(lock_id);
    if(event_id>=0)sceKernelDeleteEventFlag(event_id);
    return rc;
}
int module_stop(SceSize size,void *args) {
    (void)size;(void)args;
    SceUInt timeout=100000;
    int rc=sceKernelWaitSema(lock_id,1,&timeout);if(rc<0)return SM_BUSY;
    rc=shutdown_usb();if(rc<0){sceKernelSignalSema(lock_id,1);return rc;}
    rc=sceIoDelDrv("stm");
    if(rc<0){sceKernelSignalSema(lock_id,1);return rc;}
    rc=sceUsbbdUnregister(&driver);
    if(rc<0){sceIoAddDrv(&io_driver);sceKernelSignalSema(lock_id,1);return rc;}
    sceKernelDeleteSema(lock_id);sceKernelDeleteEventFlag(event_id);return 0;
}

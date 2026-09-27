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
static SmFrame send_frame __attribute__((aligned(64))),recv_frame __attribute__((aligned(64)));
static SceUID event_id=-1,lock_id=-1;
static volatile int attached,send_pending,recv_pending,poisoned,cancelled;
static int started;
static struct UsbDriver driver;
static unsigned char usb_string[]={26,3,'S',0,'t',0,'r',0,'e',0,'a',0,'m',0,'M',0,'a',0,'s',0,'t',0,'e',0,'r',0};
static int control_request(int a,int b,struct DeviceRequest *request){(void)a;(void)b;(void)request;return 0;}
static int control_complete(int a,int b,int c){(void)a;(void)b;(void)c;return 0;}
static int done(struct UsbdDeviceReq *r,int a,int b) {
    (void)a;(void)b;
    if(r==&send_req)send_pending=0;else recv_pending=0;
    sceKernelSetEventFlag(event_id,r==&send_req?1:2);return 0;
}
static void cancel_requests(void) {
    cancelled=1;
    if(started){sceUsbbdReqCancelAll(&endpoints[1]);sceUsbbdReqCancelAll(&endpoints[2]);}
    sceKernelSetEventFlag(event_id,4);
}
static int attach(int speed,void *a,void *b){(void)a;(void)b;attached=speed;return 0;}
static int detach(int a,int b,int c){(void)a;(void)b;(void)c;attached=0;cancel_requests();return 0;}
static int start_driver(int size,void *args) {
    (void)size;(void)args;memset(descriptors,0,sizeof(descriptors));
    struct DeviceDescriptor device={18,1,0x0200,0,0,0,64,0,0,0x0100,0,0,0,1};
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
static int stop_driver(int size,void *args){(void)size;(void)args;attached=0;cancel_requests();return 0;}
static struct UsbDriver driver={.name=DRIVER,.endpoints=3,.endp=endpoints,.intp=&interface,
    .str=(struct StringDescriptor *)usb_string,.recvctl=control_request,.func28=control_complete,
    .attach=attach,.detach=detach,.start_func=start_driver,.stop_func=stop_driver};
static int user_buffer(void *p,int bytes) {
    uintptr_t a=(uintptr_t)p;
    return bytes>=0 && a>=0x08800000U && a<0x0c000000U && (unsigned)bytes<=0x0c000000U-a;
}
static int wait_request(struct UsbdDeviceReq *request,int bit) {
    SceUInt timeout=15000000;u32 bits=0;
    int rc=sceKernelWaitEventFlag(event_id,bit|4,PSP_EVENT_WAITOR|PSP_EVENT_WAITCLEAR,&bits,&timeout);
    if(rc<0 || (bits&4) || cancelled)return SM_TIMEOUT;
    if(request->retcode || request->recvsize!=SM_FRAME_SIZE)return SM_IO;
    return SM_OK;
}
static int shutdown_usb(void) {
    cancel_requests();
    if(started)sceUsbDeactivate(SM_USB_PID);
    for(int i=0;i<100 && (send_pending||recv_pending);i++)sceKernelDelayThread(1000);
    if(send_pending || recv_pending){poisoned=1;return SM_BUSY;}
    if(started){sceUsbStop(DRIVER,0,NULL);sceUsbStop(PSP_USBBUS_DRIVERNAME,0,NULL);started=0;}
    memset(&send_frame,0,sizeof(send_frame));memset(&recv_frame,0,sizeof(recv_frame));
    attached=0;poisoned=0;return 0;
}
static int exchange(const void *in,void *out) {
    if(!started || !attached)return SM_OFFLINE;
    if(poisoned || send_pending || recv_pending)return SM_BUSY;
    cancelled=0;memcpy(&send_frame,in,sizeof(send_frame));
    if(!sm_valid(&send_frame) || send_frame.flags)return SM_INVALID;
    memset(&recv_frame,0,sizeof(recv_frame));memset(&send_req,0,sizeof(send_req));memset(&recv_req,0,sizeof(recv_req));
    send_req.endp=&endpoints[1];send_req.data=&send_frame;send_req.size=SM_FRAME_SIZE;send_req.func=done;
    recv_req.endp=&endpoints[2];recv_req.data=&recv_frame;recv_req.size=SM_FRAME_SIZE;recv_req.func=done;
    sceKernelClearEventFlag(event_id,0);
    sceKernelDcacheWritebackRange(&send_frame,sizeof(send_frame));
    sceKernelDcacheWritebackInvalidateRange(&recv_frame,sizeof(recv_frame));
    recv_pending=1;
    int rc=sceUsbbdReqRecv(&recv_req);if(rc<0){recv_pending=0;return rc;}
    send_pending=1;rc=sceUsbbdReqSend(&send_req);
    if(rc<0)send_pending=0;
    if(rc>=0)rc=wait_request(&send_req,1);
    if(rc>=0)rc=wait_request(&recv_req,2);
    if(rc>=0) {
        sceKernelDcacheInvalidateRange(&recv_frame,sizeof(recv_frame));
        if(!sm_valid(&recv_frame)||recv_frame.flags!=SM_REPLY || recv_frame.sequence!=send_frame.sequence || recv_frame.op!=send_frame.op)rc=SM_IO;
        else memcpy(out,&recv_frame,sizeof(recv_frame));
    }
    if(rc<0){poisoned=1;cancel_requests();}
    return rc;
}
static int devctl(PspIoDrvFileArg *a,const char *name,unsigned cmd,void *in,int inlen,void *out,int outlen) {
    (void)a;(void)name;
    if(cmd==SM_DEV_CANCEL){cancel_requests();return 0;}
    if(cmd==SM_DEV_STATUS)return attached?attached:poisoned?SM_BUSY:0;
    if(cmd==SM_DEV_EXCHANGE && (inlen!=SM_FRAME_SIZE || outlen!=SM_FRAME_SIZE || !user_buffer(in,inlen)||!user_buffer(out,outlen)))return SM_INVALID;
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
    else if(cmd==SM_DEV_EXCHANGE)rc=exchange(in,out);
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

/* Compile the actual firmware discovery/diagnostic functions with USB stubs. */
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "streammaster/protocol.h"
#define ESP_OK 0
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define xQueueReset(x) ((void)0)
#define xTaskNotifyGive(x) ((void)0)
typedef struct {uint16_t idVendor,idProduct;uint8_t bDeviceClass,bDeviceSubClass,bDeviceProtocol;} usb_device_desc_t;
typedef struct {uint8_t len,type;uint16_t wTotalLength;} usb_config_desc_t;
static usb_device_desc_t descriptor={0x0a12,1,0xe0,1,1};
/* CSR8510 HCI interface 0, then unused SCO interface 1; real MPS/addresses. */
static _Alignas(4) uint8_t configuration[]={
    9,2,57,0,2,1,0,0xe0,50,
    9,4,0,0,3,0xe0,1,1,0,
    7,5,0x81,3,16,0,1, 7,5,2,2,64,0,1, 7,5,0x82,2,64,0,1,
    9,4,1,0,2,0xe0,1,1,0, 7,5,3,1,0,0,1, 2,0};
static void *dev,*client;
static int iface,ep_evt,ep_in,ep_out,evt_mps,acl_mps,evt_done,acl_done;
static int opens_rc,claim_rc,claims,closes;
static atomic_int online,failed;
typedef struct {unsigned unused;} Assembly;
static Assembly events,acls;
static SmBtStatus status;
static SmBtUsbDiag usb_diag;
static int usb_host_device_open(void *c,unsigned a,void **out){(void)c;(void)a;if(!opens_rc)*out=(void*)1;return opens_rc;}
static int usb_host_get_device_descriptor(void *d,const usb_device_desc_t **out){assert(d);*out=&descriptor;return 0;}
static int usb_host_get_active_config_descriptor(void *d,const usb_config_desc_t **out){assert(d);*out=(void*)configuration;return 0;}
static int usb_host_interface_claim(void *c,void *d,int i,int a){(void)c;assert(d&&i==0&&a==0);claims++;return claim_rc;}
static int usb_host_device_close(void *c,void *d){(void)c;assert(d);closes++;return 0;}
static void state(unsigned s,int e){status.state=s;status.error=e;}
/* RECORD */
/* OPEN */
int main(void){
    claim_rc=ESP_ERR_NOT_SUPPORTED;
    assert(!open_adapter(2)&&!dev&&claims==1&&closes==1);
    assert(status.vid==0x0a12&&status.state==SM_BT_ERROR&&status.error==claim_rc);
    assert(usb_diag.count==1&&usb_diag.probe[0].phase==SM_BT_PROBE_CLAIM);
    assert(usb_diag.probe[0].interface_class==0xe00101&&usb_diag.probe[0].endpoints==0x028281);
    claim_rc=0;assert(open_adapter(2)&&claims==2&&online);
    assert(usb_diag.count==1&&usb_diag.probe[0].attempts==2&&usb_diag.probe[0].phase==SM_BT_PROBE_STARTED);
    assert(iface==0&&evt_mps==16&&acl_mps==64);dev=NULL;
    descriptor.idVendor=0x054c;assert(!open_adapter(3));
    assert(usb_diag.count==2&&usb_diag.probe[1].phase==SM_BT_PROBE_FILTER&&claims==2);
    opens_rc=0x103;assert(!open_adapter(4));
    assert(usb_diag.probe[2].phase==SM_BT_PROBE_OPEN&&usb_diag.probe[2].result==0x103);
    for(unsigned a=5;a<100;a++)assert(!open_adapter(a));
    assert(usb_diag.count==8);
    puts("Actual USB discovery: CSR interface/endpoints, claim failure, filtering and bounded diagnostics OK");
}

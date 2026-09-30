#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t TickType_t;
static TickType_t ticks;
#define pdMS_TO_TICKS(x) (x)
#define xTaskGetTickCount() ticks
#define EXT_HUB_ENTER_CRITICAL() ((void)0)
#define EXT_HUB_EXIT_CRITICAL() ((void)0)
#define ESP_ERROR_CHECK(x) assert((x)==0)
#define EXT_HUB_STATE_CONFIGURED 1
#define EXT_HUB_STAGE_IDLE 0
#define USB_B_REQUEST_HUB_GET_PORT_STATUS 0
#define TAILQ_FOREACH(var, head, member) for((var)=*(head);(var);(var)=(var)->next)
typedef struct {uint8_t bytes[8];} usb_setup_packet_t;
#define USB_SETUP_PACKET_GET_PORT(p) ((p)->bytes[4])
typedef struct {struct {uint16_t val;} wPortStatus,wPortChange;} usb_port_status_t;
typedef struct {uint8_t *data_buffer;} usb_transfer_t;
typedef struct hub {
    struct hub *next;
    struct {int state,stage;unsigned maxchild;bool sm_poll_ready,sm_poll_inflight;} single_thread;
    struct {struct {bool waiting_release,is_gone;} flags;} dynamic;
    struct {struct {usb_transfer_t transfer;} *ctrl_urb;
        struct {unsigned bNbrPorts;} *hub_desc;void **ports;} constant;
} ext_hub_dev_t;
static unsigned status_requests,status_deliveries,raw_requests,last_port;
static int get_status(void *p){assert(p);status_requests++;return 0;}
static int set_status(void *p,const usb_port_status_t *s){assert(p&&s);status_deliveries++;return 0;}
static struct {int(*get_status)(void *);int(*set_status)(void *,const usb_port_status_t *);} port_api={get_status,set_status};
static struct driver {struct {ext_hub_dev_t *ext_hubs_tailq;} dynamic;
    struct {__typeof__(port_api) *port_driver;} constant;} driver;
static struct driver *p_ext_hub_driver=&driver;
static int ext_hub_control_request(ext_hub_dev_t *h,unsigned port,int request,int feature){
    assert(h&&port>0&&port<=h->single_thread.maxchild);(void)request;(void)feature;
    raw_requests++;last_port=port;h->single_thread.stage=1;return 0;
}
/* RESPONSE */
/* POLL */
int main(void){
    _Alignas(8) uint8_t buffer[12]={0};
    __typeof__(*((ext_hub_dev_t*)0)->constant.ctrl_urb) urb={.transfer={buffer}};
    __typeof__(*((ext_hub_dev_t*)0)->constant.hub_desc) desc={.bNbrPorts=2};
    int port[2];void *ports[]={&port[0],&port[1]};
    ext_hub_dev_t hub={.single_thread={.state=1,.maxchild=2},
        .constant={.ctrl_urb=&urb,.hub_desc=&desc,.ports=ports}};
    driver.dynamic.ext_hubs_tailq=&hub;driver.constant.port_driver=&port_api;
    ticks=100;sm_usb_hub_poll();assert(!raw_requests); /* initial/reset sequence busy */
    hub.single_thread.sm_poll_ready=true;ticks+=50;sm_usb_hub_poll();
    assert(raw_requests==1&&last_port==1&&!status_requests&&hub.single_thread.sm_poll_inflight);
    ticks+=50;sm_usb_hub_poll();assert(raw_requests==1); /* EP0 in flight */
    buffer[4]=1;handle_port_status(&hub);hub.single_thread.stage=0;
    assert(!status_requests&&!status_deliveries&&hub.single_thread.sm_poll_ready);
    /* Repeated unchanged statuses must never emit reset-completed events. */
    for(unsigned i=0;i<100;i++){
        ticks+=50;sm_usb_hub_poll();buffer[4]=last_port;handle_port_status(&hub);hub.single_thread.stage=0;
    }
    assert(!status_requests&&!status_deliveries);
    ticks+=50;sm_usb_hub_poll();buffer[4]=last_port;
    ((usb_port_status_t*)(buffer+8))->wPortChange.val=1;handle_port_status(&hub);hub.single_thread.stage=0;
    assert(status_requests==1&&!status_deliveries&&!hub.single_thread.sm_poll_ready);
    unsigned before=raw_requests;ticks+=50;sm_usb_hub_poll();assert(raw_requests==before);
    /* The normal driver response still takes the original route. */
    handle_port_status(&hub);assert(status_deliveries==1);
    hub.single_thread.sm_poll_ready=true;hub.dynamic.flags.waiting_release=true;
    ticks+=50;sm_usb_hub_poll();assert(raw_requests==before);
    hub.dynamic.flags.waiting_release=false;hub.dynamic.flags.is_gone=true;
    ticks+=50;sm_usb_hub_poll();assert(raw_requests==before);
    puts("Hub: unchanged status, change handoff, busy/reset and unplug guards OK");
}

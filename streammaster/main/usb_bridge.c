/* SPDX-License-Identifier: GPL-2.0-or-later
 * One USB owner; slow Wi-Fi/HTTP work never runs in the host event loop. */
#include "bridge.h"
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "usb/usb_host.h"
#include "esp_timer.h"
#include "esp_log.h"
typedef struct {uint32_t epoch;SmFrame frame;} Work;
static QueueHandle_t commands,replies;
static usb_host_client_handle_t client;
static usb_device_handle_t device;
static usb_transfer_t *rx,*tx;
static int new_address,gone,claimed,iface,rx_pending,tx_pending,rx_done,tx_done,busy;
static uint32_t epoch;
static int64_t tx_deadline;
static Work job,answer;
static void network_worker(void *unused) {
    (void)unused;static Work work,response;
    for(;;) {
        if(xQueueReceive(commands,&work,pdMS_TO_TICKS(100))==pdTRUE) {
            sm_network_idle();
            response.epoch=work.epoch;sm_network_command(&work.frame,&response.frame);
            memset(&work,0,sizeof(work));xQueueOverwrite(replies,&response);memset(&response,0,sizeof(response));
            usb_host_client_unblock(client);
        }
        sm_network_idle();
    }
}
static void client_event(const usb_host_client_event_msg_t *event,void *arg) {
    (void)arg;
    if(event->event==USB_HOST_CLIENT_EVENT_NEW_DEV && !device)new_address=event->new_dev.address;
    if(event->event==USB_HOST_CLIENT_EVENT_DEV_GONE && event->dev_gone.dev_hdl==device)gone=1;
}
static void transfer_done(usb_transfer_t *transfer) {
    if(transfer==rx){rx_pending=0;rx_done=1;}
    else {tx_pending=0;tx_done=1;}
}
static void flush_endpoints(void) {
    if(!device || !claimed)return;
    usb_host_endpoint_halt(device,0x81);usb_host_endpoint_flush(device,0x81);
    usb_host_endpoint_halt(device,0x02);usb_host_endpoint_flush(device,0x02);
}
static int open_psp(int address) {
    if(usb_host_device_open(client,address,&device)!=ESP_OK)return 0;
    const usb_device_desc_t *desc;const usb_config_desc_t *config;
    if(usb_host_get_device_descriptor(device,&desc)!=ESP_OK || desc->idVendor!=0x054c || desc->idProduct!=SM_USB_PID ||
       usb_host_get_active_config_descriptor(device,&config)!=ESP_OK)goto reject;
    const uint8_t *p=(const uint8_t *)config,*end=p+config->wTotalLength;
    int candidate=-1,found_in=0,found_out=0;
    while(p+2<=end && p[0]>=2 && p+p[0]<=end) {
        if(p[1]==USB_B_DESCRIPTOR_TYPE_INTERFACE && p[0]>=9) {
            const usb_intf_desc_t *it=(const usb_intf_desc_t *)p;
            candidate=it->bInterfaceClass==0xff && it->bInterfaceSubClass==SM_USB_SUBCLASS &&
                it->bInterfaceProtocol==SM_USB_PROTOCOL && !it->bAlternateSetting?it->bInterfaceNumber:-1;
            found_in=found_out=0;
        } else if(candidate>=0 && p[1]==USB_B_DESCRIPTOR_TYPE_ENDPOINT && p[0]>=7) {
            const usb_ep_desc_t *ep=(const usb_ep_desc_t *)p;
            if((ep->bmAttributes&3)==2 && ep->wMaxPacketSize==64) {
                if(ep->bEndpointAddress==0x81)found_in=1;
                if(ep->bEndpointAddress==0x02)found_out=1;
            }
            if(found_in && found_out) {
                iface=candidate;
                if(usb_host_interface_claim(client,device,iface,0)!=ESP_OK)goto reject;
                claimed=1;epoch++;busy=rx_done=tx_done=0;return 1;
            }
        }
        p+=p[0];
    }
reject:usb_host_device_close(client,device);device=NULL;return 0;
}
void sm_usb_daemon(void *unused) {
    (void)unused;
    for(;;){uint32_t flags;usb_host_lib_handle_events(portMAX_DELAY,&flags);}
}
void sm_usb_task(void *unused) {
    (void)unused;
    commands=xQueueCreate(1,sizeof(Work));replies=xQueueCreate(1,sizeof(Work));
    if(!commands || !replies)abort();
    usb_host_client_config_t cfg={.is_synchronous=false,.max_num_event_msg=8,
        .async={.client_event_callback=client_event,.callback_arg=NULL}};
    ESP_ERROR_CHECK(usb_host_client_register(&cfg,&client));
    ESP_ERROR_CHECK(usb_host_transfer_alloc(SM_FRAME_SIZE,0,&rx));
    ESP_ERROR_CHECK(usb_host_transfer_alloc(SM_FRAME_SIZE,0,&tx));
    rx->callback=transfer_done;tx->callback=transfer_done;
    if(xTaskCreate(network_worker,"network",12288,NULL,6,NULL)!=pdPASS)abort();
    for(;;) {
        usb_host_client_handle_events(client,pdMS_TO_TICKS(20));
        if(new_address && !device){int address=new_address;new_address=0;open_psp(address);}
        if(gone) {
            sm_network_drop_http();
            flush_endpoints();
            /* Static transfer buffers remain alive until both callbacks have
             * completed. Never free or reuse DMA memory on an unplug timeout. */
            if(rx_pending || tx_pending)continue;
            if(claimed)usb_host_interface_release(client,device,iface);
            if(device)usb_host_device_close(client,device);
            device=NULL;claimed=gone=busy=rx_done=tx_done=0;epoch++;
            memset(rx->data_buffer,0,SM_FRAME_SIZE);memset(tx->data_buffer,0,SM_FRAME_SIZE);continue;
        }
        if(!claimed)continue;
        if(tx_pending && esp_timer_get_time()>tx_deadline){gone=1;continue;}
        if(tx_done){tx_done=0;if(tx->status!=USB_TRANSFER_STATUS_COMPLETED || tx->actual_num_bytes!=SM_FRAME_SIZE){gone=1;continue;}busy=0;}
        if(rx_done) {
            rx_done=0;
            if(rx->status!=USB_TRANSFER_STATUS_COMPLETED || rx->actual_num_bytes!=SM_FRAME_SIZE){gone=1;continue;}
            memcpy(&job.frame,rx->data_buffer,SM_FRAME_SIZE);job.epoch=epoch;
            memset(rx->data_buffer,0,SM_FRAME_SIZE);busy=1;xQueueOverwrite(commands,&job);memset(&job,0,sizeof(job));
        }
        if(!tx_pending && xQueueReceive(replies,&answer,0)==pdTRUE) {
            if(answer.epoch==epoch) {
                memcpy(tx->data_buffer,&answer.frame,SM_FRAME_SIZE);tx->device_handle=device;
                tx->bEndpointAddress=0x02;tx->num_bytes=SM_FRAME_SIZE;
                if(usb_host_transfer_submit(tx)!=ESP_OK)gone=1;
                else {tx_pending=1;tx_deadline=esp_timer_get_time()+5000000;}
            }
            memset(&answer,0,sizeof(answer));
        }
        if(!busy && !rx_pending && !gone) {
            rx->device_handle=device;rx->bEndpointAddress=0x81;rx->num_bytes=SM_FRAME_SIZE;
            if(usb_host_transfer_submit(rx)!=ESP_OK)gone=1;else rx_pending=1;
        }
    }
}

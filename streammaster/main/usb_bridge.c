/* SPDX-License-Identifier: GPL-2.0-or-later
 * One USB owner; slow Wi-Fi/HTTP work never runs in the host event loop. */
#include "bridge.h"
#include <stdlib.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "usb/usb_host.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
typedef struct {uint32_t epoch;int64_t accepted_us;SmFrame frame;} Work;
typedef struct {
    uint32_t epoch,wire_size;int64_t ready_us;
    uint32_t queue_us,work_us,ring_copy_us,checksum_us;
    union {SmFrame frame;SmBulkFrame bulk;} packet;
} Reply;
static QueueHandle_t commands,replies;
static StaticQueue_t commands_control,replies_control;
static usb_host_client_handle_t client;
static usb_device_handle_t device;
static usb_transfer_t *rx,*tx;
static int new_address,gone,claimed,iface,rx_pending,tx_pending,rx_done,tx_done,busy;
static atomic_uint epoch;
static int64_t tx_deadline;
static int64_t tx_started_us,last_bulk_done_us;
static unsigned tx_bulk_bytes;static int tx_bulk;
static SmUsbMetrics metrics;
static portMUX_TYPE metrics_lock=portMUX_INITIALIZER_UNLOCKED;
void sm_usb_metrics_snapshot(SmUsbMetrics *out) {
    portENTER_CRITICAL(&metrics_lock);*out=metrics;portEXIT_CRITICAL(&metrics_lock);
}
static Work job;
static Reply *answer;
/* Queue controls and USB DMA stay internal; only CPU-accessed packet storage
 * belongs in PSRAM. FreeRTOS xQueueCreate always allocates internal RAM. */
static void *bridge_storage(size_t size) {
    void *p=heap_caps_calloc(1,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!p){ESP_LOGE("streammaster","PSRAM allocation failed: %u bytes",(unsigned)size);abort();}
    return p;
}
static void memory_report(const char *stage) {
    ESP_LOGI("streammaster","%s: internal free=%u largest=%u DMA largest=%u PSRAM free=%u",stage,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
static void mark_gone(void) {
    if(!gone){atomic_fetch_add(&epoch,1);gone=1;}
}
static int process_work(Work *work,Work *response) {
    sm_network_idle();
    if(work->epoch!=atomic_load(&epoch))return 0;
    response->epoch=work->epoch;sm_network_command(&work->frame,&response->frame);
    /* A slow diagnostic/connect operation may outlive its USB link.
     * Discard both its reply and any connection it just opened. */
    if(work->epoch!=atomic_load(&epoch)) {
        sm_sockets_reset();sm_network_drop_http();sm_network_idle();return 0;
    }
    return 1;
}
static void network_worker(void *unused) {
    (void)unused;static Work work,response;
    Reply *reply=bridge_storage(sizeof(*reply));
    for(;;) {
        if(xQueueReceive(commands,&work,pdMS_TO_TICKS(100))==pdTRUE) {
            int valid=0;reply->epoch=work.epoch;
            if(work.frame.op==SM_SOCKET_READ_BULK || work.frame.op==SM_SOCKET_READ_BULK_EXT) {
                int64_t begin=esp_timer_get_time();reply->queue_us=begin-work.accepted_us;
                sm_network_idle();
                if(work.epoch==atomic_load(&epoch)) {
                    sm_sockets_bulk_read(&work.frame,&reply->packet.bulk);
                    reply->wire_size=sm_bulk_wire_size_op(reply->packet.bulk.op,reply->packet.bulk.length);
                    sm_sockets_bulk_cost(&reply->ring_copy_us,&reply->checksum_us);
                    reply->work_us=esp_timer_get_time()-begin;
                    valid=work.epoch==atomic_load(&epoch);
                }
            } else if(process_work(&work,&response)) {
                reply->wire_size=work.frame.flags==SM_COMPACT?sm_wire_size(response.frame.length):SM_FRAME_SIZE;
                memcpy(&reply->packet.frame,&response.frame,reply->wire_size);valid=1;
            }
            if(valid) {
                reply->ready_us=esp_timer_get_time();
                /* Bounded FIFO entries, never overwrite an unread reply. On
                 * detach, stale epochs are discarded by the USB owner. */
                while(work.epoch==atomic_load(&epoch) && xQueueSend(replies,reply,pdMS_TO_TICKS(20))!=pdTRUE)
                    usb_host_client_unblock(client);
                usb_host_client_unblock(client);
            }
            memset(&work,0,sizeof(work));memset(&response,0,sizeof(response));
        }
        sm_network_idle();
    }
}
static void client_event(const usb_host_client_event_msg_t *event,void *arg) {
    (void)arg;
    if(event->event==USB_HOST_CLIENT_EVENT_NEW_DEV && (!device || gone))new_address=event->new_dev.address;
    if(event->event==USB_HOST_CLIENT_EVENT_DEV_GONE && event->dev_gone.dev_hdl==device)mark_gone();
}
static void transfer_done(usb_transfer_t *transfer) {
    if(transfer==rx){rx_pending=0;rx_done=1;}
    else {
        if(tx_bulk) {
            int64_t now=esp_timer_get_time();unsigned duration=now-tx_started_us;
            portENTER_CRITICAL(&metrics_lock);
            metrics.tx_us+=duration;if(duration>metrics.tx_max_us)metrics.tx_max_us=duration;
            if(transfer->status==USB_TRANSFER_STATUS_COMPLETED){metrics.bytes+=tx_bulk_bytes;metrics.requests++;}
            portEXIT_CRITICAL(&metrics_lock);last_bulk_done_us=now;
        } else last_bulk_done_us=0;
        tx_pending=0;tx_done=1;
    }
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
                claimed=1;sm_led_usb(1);epoch++;busy=rx_done=tx_done=0;return 1;
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
    memory_report("USB init");
    commands=xQueueCreateStatic(SM_BULK_MAX_DEPTH,sizeof(Work),
        bridge_storage(SM_BULK_MAX_DEPTH*sizeof(Work)),&commands_control);
    replies=xQueueCreateStatic(SM_BULK_MAX_DEPTH,sizeof(Reply),
        bridge_storage(SM_BULK_MAX_DEPTH*sizeof(Reply)),&replies_control);
    answer=bridge_storage(sizeof(*answer));
    if(!commands || !replies){ESP_LOGE("streammaster","Queue initialization failed");abort();}
    usb_host_client_config_t cfg={.is_synchronous=false,.max_num_event_msg=8,
        .async={.client_event_callback=client_event,.callback_arg=NULL}};
    ESP_ERROR_CHECK(usb_host_client_register(&cfg,&client));
    ESP_ERROR_CHECK(usb_host_transfer_alloc(SM_FRAME_SIZE,0,&rx));
    ESP_ERROR_CHECK(usb_host_transfer_alloc(SM_BULK_MAX_FRAME_SIZE,0,&tx));
    rx->callback=transfer_done;tx->callback=transfer_done;
    if(xTaskCreate(network_worker,"network",12288,NULL,6,NULL)!=pdPASS){memory_report("worker allocation failed");abort();}
    memory_report("USB ready");
    for(;;) {
        usb_host_client_handle_events(client,pdMS_TO_TICKS(20));
        if(new_address && !device){int address=new_address;new_address=0;open_psp(address);}
        if(gone) {
            sm_sockets_reset();
            sm_led_usb(0);
            sm_network_drop_http();
            flush_endpoints();
            /* Static transfer buffers remain alive until both callbacks have
             * completed. Never free or reuse DMA memory on an unplug timeout. */
            if(rx_pending || tx_pending)continue;
            xQueueReset(commands);xQueueReset(replies);
            /* Never lose a live handle when release temporarily fails. */
            if(claimed) {
                if(usb_host_interface_release(client,device,iface)!=ESP_OK)continue;
                claimed=0;
            }
            if(device && usb_host_device_close(client,device)!=ESP_OK)continue;
            device=NULL;claimed=gone=busy=rx_done=tx_done=0;epoch++;
            last_bulk_done_us=0;
            memset(rx->data_buffer,0,SM_FRAME_SIZE);memset(tx->data_buffer,0,SM_BULK_MAX_FRAME_SIZE);continue;
        }
        if(!claimed)continue;
        if(tx_pending && esp_timer_get_time()>tx_deadline){mark_gone();continue;}
        if(tx_done){tx_done=0;if(tx->status!=USB_TRANSFER_STATUS_COMPLETED || tx->actual_num_bytes!=tx->num_bytes){mark_gone();continue;}if(busy)busy--;if(!busy)last_bulk_done_us=0;}
        if(rx_done) {
            rx_done=0;
            if(rx->status!=USB_TRANSFER_STATUS_COMPLETED || rx->actual_num_bytes<32){mark_gone();continue;}
            SmFrame *frame=(SmFrame *)rx->data_buffer;
            if(!sm_request_wire_valid(frame,rx->actual_num_bytes)){mark_gone();continue;}
            memset(&job.frame,0,sizeof(job.frame));
            memcpy(&job.frame,rx->data_buffer,32+frame->length);job.epoch=epoch;job.accepted_us=esp_timer_get_time();
            busy++;
            if(xQueueSend(commands,&job,0)!=pdTRUE){mark_gone();continue;}
            memset(&job,0,sizeof(job));
        }
        if(!tx_pending && xQueueReceive(replies,answer,0)==pdTRUE) {
            if(answer->epoch==epoch) {
                int64_t begin=esp_timer_get_time();
                memcpy(tx->data_buffer,&answer->packet,answer->wire_size);tx->device_handle=device;
                tx->bEndpointAddress=0x02;tx->num_bytes=answer->wire_size;
                tx_bulk=answer->packet.frame.op==SM_SOCKET_READ_BULK || answer->packet.frame.op==SM_SOCKET_READ_BULK_EXT;
                tx_bulk_bytes=answer->packet.frame.length;tx_started_us=esp_timer_get_time();
                if(tx_bulk) {
                    portENTER_CRITICAL(&metrics_lock);
                    metrics.queue_us+=answer->queue_us;metrics.work_us+=answer->work_us;
                    metrics.ring_copy_us+=answer->ring_copy_us;metrics.checksum_us+=answer->checksum_us;
                    metrics.reply_wait_us+=begin-answer->ready_us;metrics.copy_us+=tx_started_us-begin;
                    if(answer->queue_us>metrics.queue_max_us)metrics.queue_max_us=answer->queue_us;
                    if(last_bulk_done_us)metrics.gap_us+=tx_started_us-last_bulk_done_us;
                    portEXIT_CRITICAL(&metrics_lock);
                }
                if(usb_host_transfer_submit(tx)!=ESP_OK)mark_gone();
                else {tx_pending=1;tx_deadline=esp_timer_get_time()+5000000;}
            }
            memset(answer,0,sizeof(*answer));
        }
        /* Accept more requests while a response is on the bus. At most four
         * commands are outstanding, bounding control latency and memory. */
        if(busy<(int)SM_BULK_MAX_DEPTH && !rx_pending && !gone) {
            rx->device_handle=device;rx->bEndpointAddress=0x81;rx->num_bytes=SM_FRAME_SIZE;
            if(usb_host_transfer_submit(rx)!=ESP_OK)mark_gone();else rx_pending=1;
        }
    }
}

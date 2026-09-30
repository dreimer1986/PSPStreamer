/* SPDX-License-Identifier: GPL-2.0-or-later
 * External USB HCI controller. Never uses the S3's BLE-only radio. */
#include "bridge.h"
#include "gamepad.h"
#include "esp_bluedroid_hci.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_hidh_api.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "usb/usb_host.h"
#include <stdatomic.h>
#include <stdio.h>

static const char *TAG="gamepad";
static portMUX_TYPE guard=portMUX_INITIALIZER_UNLOCKED;
static SmBtStatus status;
static SmBtUsbDiag usb_diag;
static void record_probe(SmBtUsbProbe *p) {
    portENTER_CRITICAL(&guard);
    unsigned i;
    for(i=0;i<usb_diag.count;i++)if(usb_diag.probe[i].address==p->address)break;
    if(i==8)i=7;
    unsigned attempts=i<usb_diag.count?usb_diag.probe[i].attempts:0;
    usb_diag.probe[i]=*p;usb_diag.probe[i].attempts=attempts+1;
    if(i==usb_diag.count)usb_diag.count++;
    portEXIT_CRITICAL(&guard);
}
static SmPad pad={.magic=SM_PAD_MAGIC,.x=128,.y=128};
static SmHidMap map;
static SmHidPad hidpad={.x=128,.y=128};
static usb_host_client_handle_t client;
static usb_device_handle_t dev;
static usb_transfer_t *evt,*acl,*tx;
static int addr,iface=-1,ep_evt,ep_in,ep_out,evt_mps,acl_mps;
static int evt_pending,acl_pending,tx_pending,evt_done,acl_done;
static atomic_int online,ready,failed;
static atomic_int accept_input;
static int gone;
static QueueHandle_t sends,actions;
static TaskHandle_t lifecycle;
static _Atomic(const esp_bluedroid_hci_driver_callbacks_t *) host;
static atomic_int callback_users,closing;
typedef struct {uint16_t len;uint8_t data[1028];} Packet;
static Packet outgoing;
typedef struct {uint8_t data[1030];unsigned used,want;} Assembly;
static Assembly events,acls;
static uint8_t peer[6];
static int64_t pair_until;
static int64_t last_input_log;
static int pairing_allowed(const uint8_t address[6]) {
    int64_t now=esp_timer_get_time();int allow;
    portENTER_CRITICAL(&guard);allow=now<pair_until&&!memcmp(peer,address,6);portEXIT_CRITICAL(&guard);
    return allow;
}
/* Stop entering host callbacks before deinit frees their queues. A callback
 * already executing must leave first; USB buffers themselves remain static. */
static const esp_bluedroid_hci_driver_callbacks_t *callback_enter(void) {
    atomic_fetch_add(&callback_users,1);
    const esp_bluedroid_hci_driver_callbacks_t *cb=atomic_load(&host);
    if(atomic_load(&closing)||!cb){atomic_fetch_sub(&callback_users,1);return NULL;}
    return cb;
}
static void callback_leave(void){atomic_fetch_sub(&callback_users,1);}
static void neutral(void) {
    portENTER_CRITICAL(&guard);pad.buttons=0;pad.x=pad.y=128;pad.connected=0;pad.sequence++;portEXIT_CRITICAL(&guard);
}
static void state(unsigned s,int err) {
    portENTER_CRITICAL(&guard);status.state=s;status.error=err;portEXIT_CRITICAL(&guard);
}
void sm_gamepad_snapshot(SmPad *out) {
    portENTER_CRITICAL(&guard);*out=pad;portEXIT_CRITICAL(&guard);
}
int sm_gamepad_command(const SmFrame *r,SmFrame *out) {
    if(r->op==SM_BT_USB_DIAG && !r->length) {
        portENTER_CRITICAL(&guard);memcpy(out->payload,&usb_diag,sizeof(usb_diag));portEXIT_CRITICAL(&guard);
        out->length=sizeof(usb_diag);return SM_OK;
    }
    if(r->op==SM_BT_STATUS && !r->length) {
        portENTER_CRITICAL(&guard);memcpy(out->payload,&status,sizeof(status));portEXIT_CRITICAL(&guard);
        out->length=sizeof(status);return SM_OK;
    }
    if(r->op!=SM_BT_ACTION || r->length!=sizeof(SmBtAction))return SM_INVALID;
    SmBtAction a;memcpy(&a,r->payload,sizeof(a));
    if(a.action<SM_BT_SCAN || a.action>SM_BT_FORGET)return SM_INVALID;
    if(!atomic_load(&ready))return SM_OFFLINE;
    return xQueueSend(actions,&a,0)==pdTRUE?SM_OK:SM_BUSY;
}
static void gap(esp_bt_gap_cb_event_t e,esp_bt_gap_cb_param_t *p) {
    if(e==ESP_BT_GAP_DISC_RES_EVT) {
        SmBtDevice d={0};memcpy(d.address,p->disc_res.bda,6);
        snprintf(d.name,sizeof(d.name),"%02X:%02X:%02X:%02X:%02X:%02X",d.address[0],d.address[1],d.address[2],d.address[3],d.address[4],d.address[5]);
        for(int i=0;i<p->disc_res.num_prop;i++) {
            esp_bt_gap_dev_prop_t *v=&p->disc_res.prop[i];
            if(v->type==ESP_BT_GAP_DEV_PROP_RSSI && v->len>=1)d.rssi=*(int8_t *)v->val;
            if(v->type==ESP_BT_GAP_DEV_PROP_BDNAME && v->len>0)snprintf(d.name,sizeof(d.name),"%.*s",v->len,(char *)v->val);
            if(v->type==ESP_BT_GAP_DEV_PROP_EIR && v->len>=ESP_BT_GAP_EIR_DATA_LEN) {
                uint8_t n=0,*name=esp_bt_gap_resolve_eir_data(v->val,ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME,&n);
                if(!name)name=esp_bt_gap_resolve_eir_data(v->val,ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME,&n);
                if(name&&n)snprintf(d.name,sizeof(d.name),"%.*s",n,(char *)name);
            }
        }
        portENTER_CRITICAL(&guard);
        unsigned i;for(i=0;i<status.count;i++)if(!memcmp(status.device[i].address,d.address,6))break;
        if(i<8){status.device[i]=d;if(i==status.count)status.count++;}
        portEXIT_CRITICAL(&guard);
    } else if(e==ESP_BT_GAP_DISC_STATE_CHANGED_EVT) {
        if(p->disc_st_chg.state==ESP_BT_GAP_DISCOVERY_STOPPED) {
            portENTER_CRITICAL(&guard);if(status.state==SM_BT_SCANNING)status.state=SM_BT_READY;portEXIT_CRITICAL(&guard);
        }
    } else if(e==ESP_BT_GAP_PIN_REQ_EVT) {
        esp_bt_pin_code_t pin={'0','0','0','0'};
        int allow=pairing_allowed(p->pin_req.bda) && !p->pin_req.min_16_digit;
        esp_bt_gap_pin_reply(p->pin_req.bda,allow,allow?4:0,pin);
    } else if(e==ESP_BT_GAP_CFM_REQ_EVT) {
        esp_bt_gap_ssp_confirm_reply(p->cfm_req.bda,pairing_allowed(p->cfm_req.bda));
    } else if(e==ESP_BT_GAP_AUTH_CMPL_EVT && p->auth_cmpl.stat!=ESP_BT_STATUS_SUCCESS) {
        atomic_store(&accept_input,0);state(SM_BT_ERROR,p->auth_cmpl.stat);neutral();
    }
}
static void hid(esp_hidh_cb_event_t e,esp_hidh_cb_param_t *p) {
    if(e==ESP_HIDH_INIT_EVT) {
        if(p->init.status==ESP_HIDH_OK){
            esp_bd_addr_t bonds[4];int count=4;
            if(esp_bt_gap_get_bond_device_list(&count,bonds)==ESP_OK) {
                portENTER_CRITICAL(&guard);status.count=count;
                for(int i=0;i<count;i++) {
                    memcpy(status.device[i].address,bonds[i],6);
                    snprintf(status.device[i].name,48,"Saved %02X:%02X:%02X:%02X:%02X:%02X",bonds[i][0],bonds[i][1],bonds[i][2],bonds[i][3],bonds[i][4],bonds[i][5]);
                }
                if(count)memcpy(status.selected,bonds[0],6);
                portEXIT_CRITICAL(&guard);
            }
            atomic_store(&ready,1);state(SM_BT_READY,0);
        }
        else state(SM_BT_ERROR,p->init.status);
    } else if(e==ESP_HIDH_OPEN_EVT) {
        atomic_store(&accept_input,0);neutral();
        if(p->open.status!=ESP_HIDH_OK){state(SM_BT_ERROR,p->open.status);neutral();return;}
        memset(&map,0,sizeof(map));hidpad=(SmHidPad){.x=128,.y=128};last_input_log=0;
        portENTER_CRITICAL(&guard);memcpy(peer,p->open.bd_addr,6);memcpy(status.selected,peer,6);pair_until=0;portEXIT_CRITICAL(&guard);
        state(SM_BT_CONNECTED,0);
        esp_bt_hid_host_set_protocol(p->open.bd_addr,ESP_HIDH_REPORT_MODE);
        ESP_LOGI(TAG,"HID connected; waiting for descriptor/input");
    } else if(e==ESP_HIDH_GET_DSCP_EVT) {
        int ok=p->dscp.status==ESP_HIDH_OK && p->dscp.dsc_list && sm_hid_parse(&map,p->dscp.dsc_list,p->dscp.dl_len);
        ESP_LOGW(TAG,"HID descriptor vendor=%04x product=%04x bytes=%u fields=%u valid=%d",p->dscp.vendor_id,p->dscp.product_id,p->dscp.dl_len,map.count,ok);
        if(!ok){state(SM_BT_ERROR,SM_INVALID);neutral();}
        else atomic_store(&accept_input,1);
    } else if(e==ESP_HIDH_DATA_IND_EVT && atomic_load(&online) && atomic_load(&accept_input)) {
        if(p->data_ind.status==ESP_HIDH_OK && sm_hid_input(&map,p->data_ind.data,p->data_ind.len,&hidpad)) {
            portENTER_CRITICAL(&guard);
            pad.buttons=hidpad.buttons;pad.x=hidpad.x;pad.y=hidpad.y;pad.connected=1;pad.sequence++;status.reports++;
            portEXIT_CRITICAL(&guard);
            if(!last_input_log){ESP_LOGW(TAG,"First HID report bytes=%u buttons=%04lx axes=%u,%u",p->data_ind.len,(unsigned long)hidpad.buttons,hidpad.x,hidpad.y);last_input_log=esp_timer_get_time();}
        }
    } else if(e==ESP_HIDH_CLOSE_EVT || e==ESP_HIDH_VC_UNPLUG_EVT) {
        atomic_store(&accept_input,0);neutral();memset(&map,0,sizeof(map));hidpad=(SmHidPad){.x=128,.y=128};state(SM_BT_READY,0);
    }
}
static bool can_send(void){return atomic_load(&online) && uxQueueSpacesAvailable(sends)>0;}
static void send_packet(uint8_t *data,uint16_t len) {
    Packet p;
    if(!len||len>sizeof(p.data)||!atomic_load(&online)){atomic_store(&failed,1);return;}
    p.len=len;memcpy(p.data,data,len);
    if(xQueueSend(sends,&p,0)!=pdTRUE)atomic_store(&failed,1);
    usb_host_client_unblock(client);
}
static esp_err_t register_host(const esp_bluedroid_hci_driver_callbacks_t *cb){host=cb;return ESP_OK;}
static void lifecycle_task(void *unused) {
    (void)unused;
    for(;;) {
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        if(!atomic_load(&online))continue;
        esp_bluedroid_hci_driver_operations_t ops={.send=send_packet,.check_send_available=can_send,.register_host_callback=register_host};
        esp_err_t rc=esp_bluedroid_attach_hci_driver(&ops);
        int initialized=0,enabled=0;
        atomic_store(&closing,0);
        if(rc==ESP_OK){rc=esp_bluedroid_init();initialized=rc==ESP_OK;}
        if(rc==ESP_OK){rc=esp_bluedroid_enable();enabled=rc==ESP_OK;}
        if(rc==ESP_OK)rc=esp_bt_gap_register_callback(gap);
        if(rc==ESP_OK)rc=esp_bt_hid_host_register_callback(hid);
        if(rc==ESP_OK) {
            esp_bt_gap_set_device_name("StreamMaster Gamepad");
            esp_bt_io_cap_t io=ESP_BT_IO_CAP_NONE;esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE,&io,sizeof(io));
            rc=esp_bt_hid_host_init();
        }
        if(rc!=ESP_OK){ESP_LOGE(TAG,"Host init: %s",esp_err_to_name(rc));state(SM_BT_ERROR,rc);}
        while(atomic_load(&online) && !atomic_load(&failed) && rc==ESP_OK) {
            SmBtAction a;
            uint8_t active_peer[6];int expired=0;int64_t now=esp_timer_get_time();
            portENTER_CRITICAL(&guard);
            memcpy(active_peer,peer,6);
            if(status.state==SM_BT_CONNECTING&&pair_until&&now>=pair_until){pair_until=0;expired=1;}
            portEXIT_CRITICAL(&guard);
            if(expired){esp_bt_hid_host_disconnect(active_peer);atomic_store(&accept_input,0);neutral();state(SM_BT_ERROR,ESP_ERR_TIMEOUT);}
            if(xQueueReceive(actions,&a,pdMS_TO_TICKS(100))!=pdTRUE)continue;
            esp_err_t result=ESP_OK;
            if(a.action==SM_BT_SCAN) {
                unsigned current;portENTER_CRITICAL(&guard);current=status.state;portEXIT_CRITICAL(&guard);
                if(current==SM_BT_CONNECTED||current==SM_BT_CONNECTING){state(current,SM_BUSY);continue;}
                portENTER_CRITICAL(&guard);status.count=0;portEXIT_CRITICAL(&guard);
                state(SM_BT_SCANNING,0);result=esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY,8,8);
            } else if(a.action==SM_BT_PAIR) {
                unsigned current;portENTER_CRITICAL(&guard);current=status.state;portEXIT_CRITICAL(&guard);
                if(current==SM_BT_CONNECTED || current==SM_BT_CONNECTING){state(current,SM_BUSY);continue;}
                esp_bt_gap_cancel_discovery();atomic_store(&accept_input,0);neutral();
                int64_t deadline=esp_timer_get_time()+30000000;
                portENTER_CRITICAL(&guard);memcpy(peer,a.address,6);pair_until=deadline;portEXIT_CRITICAL(&guard);
                state(SM_BT_CONNECTING,0);result=esp_bt_hid_host_connect(a.address);
            } else if(a.action==SM_BT_DISCONNECT){atomic_store(&accept_input,0);neutral();result=esp_bt_hid_host_disconnect(active_peer);}
            else {
                atomic_store(&accept_input,0);neutral();esp_bt_hid_host_disconnect(active_peer);result=esp_bt_gap_remove_bond_device(a.address);
            }
            if(result!=ESP_OK)state(SM_BT_ERROR,result);
        }
        atomic_store(&ready,0);atomic_store(&accept_input,0);neutral();
        if(enabled){esp_bt_hid_host_deinit();esp_bluedroid_disable();}
        atomic_store(&closing,1);
        while(atomic_load(&callback_users))vTaskDelay(1);
        if(initialized)esp_bluedroid_deinit();
        /* Do not clear callback storage until the host has stopped. */
        host=NULL;esp_bluedroid_detach_hci_driver();
        state(atomic_load(&online)?SM_BT_ERROR:SM_BT_NONE,rc);
    }
}
static void receive(Assembly *a,unsigned type,const uint8_t *p,unsigned n) {
    unsigned head=type==4?2:4;
    while(n && !atomic_load(&failed)) {
        if(!a->used){a->data[0]=type;a->want=head;}
        unsigned k=a->want-a->used;if(k>n)k=n;
        memcpy(a->data+1+a->used,p,k);a->used+=k;p+=k;n-=k;
        if(a->used==head && a->want==head) {
            a->want=head+(type==4?a->data[2]:a->data[3]|(unsigned)a->data[4]<<8);
            if(a->want+1>sizeof(a->data)){atomic_store(&failed,1);return;}
        }
        if(a->used==a->want) {
            const esp_bluedroid_hci_driver_callbacks_t *cb=callback_enter();
            if(cb){if(cb->notify_host_recv && cb->notify_host_recv(a->data,a->used+1)!=0)atomic_store(&failed,1);callback_leave();}
            a->used=a->want=0;
        }
    }
}
static void complete(usb_transfer_t *t) {
    if(t==evt){evt_pending=0;evt_done=1;}
    else if(t==acl){acl_pending=0;acl_done=1;}
    else {tx_pending=0;if(t->status!=USB_TRANSFER_STATUS_COMPLETED)atomic_store(&failed,1);}
}
static void event(const usb_host_client_event_msg_t *e,void *arg) {
    (void)arg;
    if(e->event==USB_HOST_CLIENT_EVENT_NEW_DEV && !dev)addr=e->new_dev.address;
    if(e->event==USB_HOST_CLIENT_EVENT_DEV_GONE && e->dev_gone.dev_hdl==dev){gone=1;atomic_store(&online,0);atomic_store(&accept_input,0);neutral();}
}
static int open_adapter(unsigned address) {
    SmBtUsbProbe probe={.address=address,.phase=SM_BT_PROBE_OPEN};
    int known=0;
    probe.result=usb_host_device_open(client,address,&dev);
    if(probe.result!=ESP_OK){record_probe(&probe);return 0;}
    const usb_device_desc_t *d;const usb_config_desc_t *c;
    probe.phase=SM_BT_PROBE_DESCRIPTOR;
    if((probe.result=usb_host_get_device_descriptor(dev,&d))!=ESP_OK)goto reject;
    probe.vid=d->idVendor;probe.pid=d->idProduct;
    probe.device_class=(unsigned)d->bDeviceClass<<16|(unsigned)d->bDeviceSubClass<<8|d->bDeviceProtocol;
    probe.phase=SM_BT_PROBE_FILTER;
    known=(d->idVendor==0x0a12&&d->idProduct==1)||(d->idVendor==0x33fa&&d->idProduct==0x10);
    if(!known){probe.result=ESP_ERR_NOT_SUPPORTED;goto reject;}
    portENTER_CRITICAL(&guard);status.vid=d->idVendor;status.pid=d->idProduct;portEXIT_CRITICAL(&guard);
    probe.phase=SM_BT_PROBE_CONFIG;
    if((probe.result=usb_host_get_active_config_descriptor(dev,&c))!=ESP_OK)goto reject;
    probe.phase=SM_BT_PROBE_INTERFACE;
    iface=-1;ep_evt=ep_in=ep_out=0;
    const uint8_t *p=(const uint8_t *)c,*end=p+c->wTotalLength;
    while(p+2<=end && p[0]>=2 && p+p[0]<=end) {
        if(p[1]==4&&p[0]>=9) {
            if(ep_evt&&ep_in&&ep_out)break;
            if(!p[3])probe.interface_class=(unsigned)p[2]<<24|(unsigned)p[5]<<16|(unsigned)p[6]<<8|p[7];
            iface=p[5]==0xe0&&p[6]==1&&p[7]==1&&!p[3]?p[2]:-1;ep_evt=ep_in=ep_out=0;
        } else if(iface>=0&&p[1]==5&&p[0]>=7) {
            unsigned m=p[4]|p[5]<<8;
            if(m && m<=64) {
                if((p[3]&3)==3&&(p[2]&0x80)){ep_evt=p[2];evt_mps=m;}
                if((p[3]&3)==2){if(p[2]&0x80){ep_in=p[2];acl_mps=m;}else ep_out=p[2];}
            }
        }
        p+=p[0];
    }
    probe.endpoints=ep_evt|(unsigned)ep_in<<8|(unsigned)ep_out<<16;
    if(iface<0||!ep_evt||!ep_in||!ep_out){probe.result=ESP_ERR_NOT_FOUND;goto reject;}
    probe.phase=SM_BT_PROBE_CLAIM;
    if((probe.result=usb_host_interface_claim(client,dev,iface,0))!=ESP_OK)goto reject;
    probe.phase=SM_BT_PROBE_STARTED;record_probe(&probe);
    ESP_LOGW(TAG,"USB HCI %04x:%04x attached",d->idVendor,d->idProduct);
    events=(Assembly){0};acls=(Assembly){0};evt_done=acl_done=0;
    xQueueReset(sends);xQueueReset(actions);atomic_store(&failed,0);atomic_store(&online,1);state(SM_BT_STARTING,0);
    xTaskNotifyGive(lifecycle);return 1;
reject:
    record_probe(&probe);
    if(known)state(SM_BT_ERROR,probe.result);
    usb_host_device_close(client,dev);dev=NULL;iface=-1;return 0;
}
static void usb_task(void *unused) {
    (void)unused;
    usb_host_client_config_t config={.is_synchronous=false,.max_num_event_msg=8,.async={.client_event_callback=event}};
    if(usb_host_client_register(&config,&client)!=ESP_OK){state(SM_BT_ERROR,SM_IO);vTaskDelete(NULL);return;}
    if(usb_host_transfer_alloc(64,0,&evt)!=ESP_OK || usb_host_transfer_alloc(64,0,&acl)!=ESP_OK ||
       usb_host_transfer_alloc(1040,0,&tx)!=ESP_OK){
        if(evt)usb_host_transfer_free(evt);
        if(acl)usb_host_transfer_free(acl);
        if(tx)usb_host_transfer_free(tx);
        usb_host_client_deregister(client);state(SM_BT_ERROR,SM_IO);vTaskDelete(NULL);return;}
    evt->callback=acl->callback=tx->callback=complete;
    int64_t rescan=0;
    for(;;) {
        usb_host_client_handle_events(client,pdMS_TO_TICKS(10));
        if(addr&&!dev){int a=addr;addr=0;open_adapter(a);}
        if(!dev && esp_timer_get_time()>=rescan) {
            uint8_t addresses[8];int count=0;rescan=esp_timer_get_time()+1000000;
            if(usb_host_device_addr_list_fill(8,addresses,&count)==ESP_OK)
                for(int i=0;i<count&&!dev;i++)open_adapter(addresses[i]);
        }
        if(!dev)continue;
        if(gone) {
            usb_host_endpoint_halt(dev,ep_evt);usb_host_endpoint_flush(dev,ep_evt);
            usb_host_endpoint_halt(dev,ep_in);usb_host_endpoint_flush(dev,ep_in);
            usb_host_endpoint_halt(dev,ep_out);usb_host_endpoint_flush(dev,ep_out);
            if(evt_pending||acl_pending||tx_pending||host)continue;
            if(usb_host_interface_release(client,dev,iface)!=ESP_OK)continue;
            if(usb_host_device_close(client,dev)!=ESP_OK)continue;
            dev=NULL;gone=0;iface=-1;continue;
        }
        if(atomic_load(&failed)){neutral();state(SM_BT_ERROR,SM_IO);continue;}
        if(evt_done){evt_done=0;if(evt->status==USB_TRANSFER_STATUS_COMPLETED)receive(&events,4,evt->data_buffer,evt->actual_num_bytes);else atomic_store(&failed,1);}
        if(acl_done){acl_done=0;if(acl->status==USB_TRANSFER_STATUS_COMPLETED)receive(&acls,2,acl->data_buffer,acl->actual_num_bytes);else atomic_store(&failed,1);}
        if(!evt_pending){evt->device_handle=dev;evt->bEndpointAddress=ep_evt;evt->num_bytes=evt_mps;
            if(usb_host_transfer_submit(evt)==ESP_OK)evt_pending=1;else atomic_store(&failed,1);}
        if(!acl_pending){acl->device_handle=dev;acl->bEndpointAddress=ep_in;acl->num_bytes=acl_mps;
            if(usb_host_transfer_submit(acl)==ESP_OK)acl_pending=1;else atomic_store(&failed,1);}
        if(!tx_pending&&xQueueReceive(sends,&outgoing,0)==pdTRUE) {
            unsigned n=outgoing.len-1;tx->device_handle=dev;
            if(outgoing.data[0]==1) {
                usb_setup_packet_t setup={.bmRequestType=0x20,.bRequest=0,.wValue=0,.wIndex=0,.wLength=n};
                memcpy(tx->data_buffer,&setup,8);memcpy(tx->data_buffer+8,outgoing.data+1,n);
                tx->bEndpointAddress=0;tx->num_bytes=n+8;
                if(usb_host_transfer_submit_control(client,tx)==ESP_OK)tx_pending=1;else atomic_store(&failed,1);
            } else if(outgoing.data[0]==2) {
                memcpy(tx->data_buffer,outgoing.data+1,n);tx->bEndpointAddress=ep_out;tx->num_bytes=n;
                if(usb_host_transfer_submit(tx)==ESP_OK)tx_pending=1;else atomic_store(&failed,1);
            } else atomic_store(&failed,1);
            const esp_bluedroid_hci_driver_callbacks_t *cb=callback_enter();
            if(cb){if(cb->notify_host_send_available)cb->notify_host_send_available();callback_leave();}
        }
    }
}
void sm_gamepad_init(void) {
    sends=xQueueCreate(4,sizeof(Packet));actions=xQueueCreate(4,sizeof(SmBtAction));
    if(sends&&actions && xTaskCreate(lifecycle_task,"bt-host",6144,NULL,5,&lifecycle)==pdPASS &&
       xTaskCreate(usb_task,"bt-usb",6144,NULL,7,NULL)==pdPASS)return;
    if(lifecycle)vTaskDelete(lifecycle);
    if(sends)vQueueDelete(sends);
    if(actions)vQueueDelete(actions);
    state(SM_BT_ERROR,SM_IO);
}

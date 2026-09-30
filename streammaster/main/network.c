/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "bridge.h"
#include "board.h"
#include "../profiles.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_netif_sntp.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lwip/inet.h"
static SmConfig config;
static SmProfiles profiles;
static unsigned profile_tried,current_slot;
static esp_netif_t *netif;
static atomic_uint state,reason;
unsigned sm_network_state(void){return atomic_load(&state);}
static atomic_bool disconnect_requested,drop_http;
static int was_ready;
static int reconnect,retries;
static int64_t retry_at;
static uint32_t requests;
static esp_http_client_handle_t http;
static int connect_saved(int fresh);
static int save_profiles(const SmProfiles *next) {
    nvs_handle_t store;esp_err_t err=nvs_open("streammaster",NVS_READWRITE,&store);
    if(err==ESP_OK) {
        err=nvs_set_blob(store,"profiles_v1",next,sizeof(*next));
        if(err==ESP_OK)err=nvs_commit(store);
        nvs_close(store);
    }
    if(err!=ESP_OK)return SM_IO;
    profiles=*next;return SM_OK;
}
static void event(void *arg,esp_event_base_t base,int32_t id,void *data) {
    (void)arg;
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        atomic_store(&reason,((wifi_event_sta_disconnected_t *)data)->reason);
        atomic_store(&state,atomic_load(&disconnect_requested)?SM_WIFI_IDLE:SM_WIFI_FAILED);
    } else if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP)atomic_store(&state,SM_WIFI_READY);
}
static void close_http(void){if(http){esp_http_client_close(http);esp_http_client_cleanup(http);http=NULL;}}
void sm_network_drop_http(void){atomic_store(&drop_http,true);}
static void set_dns(void) {
    if(config.flags&SM_CFG_AUTO_DNS)return;
    const char *names[]={config.dns,config.dns2};
    for(int i=0;i<2;i++) {
        esp_netif_dns_info_t info={0};info.ip.type=ESP_IPADDR_TYPE_V4;
        info.ip.u_addr.ip4.addr=names[i][0]?inet_addr(names[i]):0;
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_set_dns_info(netif,i?ESP_NETIF_DNS_BACKUP:ESP_NETIF_DNS_MAIN,&info));
    }
}
static int connect_wifi(void) {
    if(!sm_config_valid(&config,0))return SM_INVALID;
    sm_sockets_reset();
    close_http();reconnect=0;esp_wifi_disconnect();atomic_store(&disconnect_requested,false);
    wifi_config_t wifi={0};
    memcpy(wifi.sta.ssid,config.ssid,strlen(config.ssid));
    memcpy(wifi.sta.password,config.password,strlen(config.password));
    wifi.sta.pmf_cfg.capable=true;wifi.sta.pmf_cfg.required=false;
    wifi.sta.scan_method=WIFI_ALL_CHANNEL_SCAN;
    wifi.sta.sort_method=WIFI_CONNECT_AP_BY_SIGNAL;
    wifi.sta.sae_pwe_h2e=WPA3_SAE_PWE_BOTH;
    if(esp_wifi_set_config(WIFI_IF_STA,&wifi)!=ESP_OK)return SM_IO;
    memset(&wifi,0,sizeof(wifi));
    esp_netif_dhcpc_stop(netif);
    esp_netif_ip_info_t ip={0};
    if(!(config.flags&SM_CFG_DHCP)) {
        ip.ip.addr=inet_addr(config.ip);ip.netmask.addr=inet_addr(config.mask);ip.gw.addr=inet_addr(config.gateway);
    }
    if(esp_netif_set_ip_info(netif,&ip)!=ESP_OK)return SM_IO;
    if(config.flags&SM_CFG_DHCP) {
        if(esp_netif_dhcpc_start(netif)!=ESP_OK)return SM_IO;
    }
    set_dns();atomic_store(&state,SM_WIFI_CONNECTING);atomic_store(&reason,0);
    reconnect=1;retries=0;retry_at=esp_timer_get_time()+15000000;
    return esp_wifi_connect()==ESP_OK?SM_OK:SM_IO;
}
static int scan_wifi(SmScan *result) {
    wifi_scan_config_t scan={.show_hidden=true};memset(result,0,sizeof(*result));
    if(esp_wifi_scan_start(&scan,true)!=ESP_OK)return SM_BUSY;
    wifi_ap_record_t *aps=calloc(24,sizeof(*aps));uint16_t count=24;
    if(!aps){esp_wifi_clear_ap_list();return SM_IO;}
    if(esp_wifi_scan_get_ap_records(&count,aps)!=ESP_OK){esp_wifi_clear_ap_list();free(aps);return SM_IO;}
    result->count=count;
    for(unsigned i=0;i<count;i++) {
        memcpy(result->ap[i].ssid,aps[i].ssid,32);result->ap[i].rssi=aps[i].rssi;
        result->ap[i].channel=aps[i].primary;result->ap[i].auth=aps[i].authmode;
    }
    free(aps);return SM_OK;
}
static int connect_saved(int fresh) {
    if(fresh)profile_tried=0;
    unsigned slot=profiles.active;
    if(profiles.automatic) {
        /* Only scan on explicit connect, startup or failed association, never
         * periodically while streaming. Work stays in the network worker. */
        atomic_store(&disconnect_requested,true);esp_wifi_disconnect();
        SmScan scan;scan_wifi(&scan);
        int picked=sm_profiles_pick(&profiles,&scan,profile_tried);
        if(picked<0) {
            int any=0;
            for(unsigned i=0;i<SM_PROFILE_COUNT;i++)if(profiles.slot[i].ssid[0])any=1;
            if(!any){reconnect=0;atomic_store(&state,SM_WIFI_IDLE);return SM_OFFLINE;}
            profile_tried=0;reconnect=1;retries=5;
            retry_at=esp_timer_get_time()+30000000;
            atomic_store(&state,SM_WIFI_FAILED);return SM_OFFLINE;
        }
        slot=(unsigned)picked;
    }
    current_slot=slot;config=profiles.slot[slot];profile_tried|=1U<<slot;
    return connect_wifi();
}
void sm_network_idle(void) {
    sm_sockets_idle();
    if(atomic_exchange(&drop_http,false))close_http();
    unsigned status=atomic_load(&state);
    if(status==SM_WIFI_READY){retries=0;profile_tried=1U<<current_slot;if(!was_ready){set_dns();esp_netif_sntp_start();}was_ready=1;return;}
    was_ready=0;
    if(reconnect && esp_timer_get_time()>=retry_at) {
        if(retries>=5){
            if(profiles.automatic)connect_saved(0);
            else {reconnect=0;atomic_store(&state,SM_WIFI_FAILED);}
            return;
        }
        esp_wifi_disconnect();esp_wifi_connect();retries++;
        atomic_store(&state,SM_WIFI_CONNECTING);
        retry_at=esp_timer_get_time()+(int64_t)(retries+1)*3000000;
    }
}
void sm_network_init(void) {
    ESP_ERROR_CHECK(nvs_flash_init()); /* Never erase another firmware's NVS automatically. */
    memset(&config,0,sizeof(config));config.flags=SM_CFG_DHCP|SM_CFG_AUTO_DNS;
    sm_profiles_init(&profiles);
    nvs_handle_t store;
    if(nvs_open("streammaster",NVS_READONLY,&store)==ESP_OK) {
        SmConfig saved;size_t size=sizeof(saved);
        if(nvs_get_blob(store,"network_v1",&saved,&size)==ESP_OK && size==sizeof(saved) && sm_config_valid(&saved,0))config=saved;
        SmProfiles stored;size=sizeof(stored);
        if(nvs_get_blob(store,"profiles_v1",&stored,&size)==ESP_OK && size==sizeof(stored) && sm_profiles_valid(&stored))profiles=stored;
        else profiles.slot[0]=config; /* Non-destructive migration from a single network. */
        memset(&stored,0,sizeof(stored));
        memset(&saved,0,sizeof(saved));nvs_close(store);
    }
    ESP_ERROR_CHECK(esp_netif_init());ESP_ERROR_CHECK(esp_event_loop_create_default());
    netif=esp_netif_create_default_wifi_sta();
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event,NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event,NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    esp_sntp_config_t time_config=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    time_config.start=false;ESP_ERROR_CHECK(esp_netif_sntp_init(&time_config));
    /* Migration is saved on the next explicit edit; do not write flash at boot. */
    current_slot=profiles.active;config=profiles.slot[current_slot];
    if(!profiles.automatic){if(config.ssid[0])connect_wifi();return;}
    /* Defer scans to the network worker so startup/USB enumeration is not
     * blocked and the smaller main-task stack never holds scan records. */
    for(unsigned i=0;i<SM_PROFILE_COUNT;i++)if(profiles.slot[i].ssid[0]) {
        reconnect=1;retries=5;retry_at=0;atomic_store(&state,SM_WIFI_CONNECTING);break;
    }
}
static int require_size(const SmFrame *r,size_t size){return r->length==size;}
void sm_network_command(const SmFrame *r,SmFrame *out) {
    memset(out,0,sizeof(*out));out->op=r->op;out->sequence=r->sequence;out->flags=SM_REPLY;requests++;
    if(!sm_valid(r) || (r->flags && r->flags!=SM_COMPACT)){out->result=SM_INVALID;goto done;}
    switch(r->op) {
    case SM_CAPABILITIES: {
        uint32_t caps=SM_CAP_COMPACT|SM_CAP_BULK_PAIR|SM_CAP_BULK_EXT|SM_CAP_USB_METRICS|SM_CAP_PROFILES|SM_CAP_NET_DIAG;
#if CONFIG_BT_BLUEDROID_ENABLED
        caps|=SM_CAP_GAMEPAD;
#endif
        memcpy(out->payload,&caps,sizeof(caps));out->length=sizeof(caps);break;
    }
#if CONFIG_BT_BLUEDROID_ENABLED
    case SM_BT_STATUS:case SM_BT_ACTION:case SM_BT_USB_DIAG:case SM_BT_OPTIONS_GET:case SM_BT_OPTIONS_SET:
    case SM_BT_SETUP_GET:case SM_BT_SETUP_SET:case SM_BT_INPUT_GET:
        out->result=sm_gamepad_command(r,out);break;
#endif
    case SM_NET_DIAG: {
        SmNetDiag d={.wifi_state=atomic_load(&state),.disconnect_reason=atomic_load(&reason),
            .internal_free=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),.rssi=-127};
        wifi_ap_record_t ap;if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK)d.rssi=ap.rssi;
        sm_sockets_diagnostic(&d);memcpy(out->payload,&d,sizeof(d));out->length=sizeof(d);break;
    }
    case SM_USB_METRICS: {
        SmUsbMetrics metrics;sm_usb_metrics_snapshot(&metrics);memcpy(out->payload,&metrics,sizeof(metrics));out->length=sizeof(metrics);break;
    }
    case SM_INFO:
    case SM_NETWORK_INFO: {
        SmInfo info={0};snprintf(info.firmware,sizeof(info.firmware),SM_BOARD_NAME " %.10s",esp_app_get_description()->version);
        info.wifi_state=atomic_load(&state);info.disconnect_reason=atomic_load(&reason);
        info.free_heap=esp_get_free_heap_size();info.usb_requests=requests;
        esp_netif_ip_info_t ip={0};esp_netif_get_ip_info(netif,&ip);
        snprintf(info.ip,sizeof(info.ip),IPSTR,IP2STR(&ip.ip));snprintf(info.gateway,sizeof(info.gateway),IPSTR,IP2STR(&ip.gw));
        for(int i=0;i<2;i++) {esp_netif_dns_info_t dns={0};esp_netif_get_dns_info(netif,i?ESP_NETIF_DNS_BACKUP:ESP_NETIF_DNS_MAIN,&dns);
            snprintf(i?info.dns2:info.dns,16,IPSTR,IP2STR(&dns.ip.u_addr.ip4));}
        wifi_ap_record_t ap;if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK)info.rssi=ap.rssi;
        if(r->op==SM_NETWORK_INFO) {
            SmNetworkInfo live={.info=info,.config_flags=config.flags};
            snprintf(live.mask,sizeof(live.mask),IPSTR,IP2STR(&ip.netmask));
            memcpy(live.ssid,config.ssid,sizeof(live.ssid));
            memcpy(out->payload,&live,sizeof(live));out->length=sizeof(live);
        } else {memcpy(out->payload,&info,sizeof(info));out->length=sizeof(info);}
        break;
    }
    case SM_CONFIG_GET: {
        SmConfig safe=config;memset(safe.password,0,sizeof(safe.password));
        if(config.password[0])safe.flags|=SM_CFG_HAS_PASSWORD;
        memcpy(out->payload,&safe,sizeof(safe));out->length=sizeof(safe);break;
    }
    case SM_CONFIG_SET: {
        if(!require_size(r,sizeof(SmConfig))){out->result=SM_INVALID;break;}
        SmConfig next;memcpy(&next,r->payload,sizeof(next));
        if(!sm_config_valid(&next,1)){out->result=SM_INVALID;memset(&next,0,sizeof(next));break;}
        if(next.flags&SM_CFG_KEEP_PASSWORD) {
            if(strcmp(next.ssid,config.ssid)){out->result=SM_INVALID;memset(&next,0,sizeof(next));break;}
            memcpy(next.password,config.password,sizeof(next.password));
        }
        next.flags&=~SM_CFG_KEEP_PASSWORD;
        SmProfiles update=profiles;update.active=current_slot;update.slot[current_slot]=next;
        out->result=save_profiles(&update);
        if(out->result==SM_OK){config=next;out->result=connect_wifi();}
        memset(&update,0,sizeof(update));
        memset(&next,0,sizeof(next));break;
    }
    case SM_SCAN: {
        SmScan result;out->result=scan_wifi(&result);
        if(out->result==SM_OK){memcpy(out->payload,&result,sizeof(result));out->length=sizeof(result);}break;
    }
    case SM_PROFILES_GET: {
        SmProfiles safe=profiles;safe.active=current_slot;sm_profiles_public(&safe);
        memcpy(out->payload,&safe,sizeof(safe));out->length=sizeof(safe);break;
    }
    case SM_PROFILE_SAVE: {
        if(!require_size(r,sizeof(SmProfileSave))){out->result=SM_INVALID;break;}
        SmProfileSave request;memcpy(&request,r->payload,sizeof(request));
        SmProfiles next=profiles;out->result=sm_profile_update(&next,request.slot,&request.config);
        if(out->result==SM_OK)out->result=save_profiles(&next);
        if(out->result==SM_OK){current_slot=request.slot;config=profiles.slot[current_slot];out->result=connect_wifi();}
        memset(&request,0,sizeof(request));memset(&next,0,sizeof(next));break;
    }
    case SM_PROFILE_SELECT:
    case SM_PROFILE_DELETE:
    case SM_PROFILE_AUTO: {
        uint32_t value;
        if(!require_size(r,sizeof(value))){out->result=SM_INVALID;break;}
        memcpy(&value,r->payload,sizeof(value));SmProfiles next=profiles;
        if(r->op==SM_PROFILE_AUTO) {
            if(value>1){out->result=SM_INVALID;break;}next.automatic=value;next.active=current_slot;
        } else {
            if(value>=SM_PROFILE_COUNT){out->result=SM_INVALID;break;}
            if(r->op==SM_PROFILE_SELECT) {
                if(!next.slot[value].ssid[0]){out->result=SM_INVALID;break;}
                next.active=value;next.automatic=0;
            } else memset(&next.slot[value],0,sizeof(next.slot[value]));
        }
        out->result=save_profiles(&next);memset(&next,0,sizeof(next));
        if(out->result==SM_OK) {
            if(r->op==SM_PROFILE_AUTO && !value)break; /* Pin the live network, no disruption. */
            if(r->op==SM_PROFILE_DELETE && value!=current_slot)break;
            reconnect=0;atomic_store(&disconnect_requested,true);esp_wifi_disconnect();
            sm_sockets_reset();close_http();atomic_store(&state,SM_WIFI_IDLE);
            out->result=connect_saved(1);
            if(out->result==SM_OFFLINE || out->result==SM_INVALID)out->result=SM_OK;
        }
        break;
    }
    case SM_CONNECT:out->result=connect_saved(1);break;
    case SM_DISCONNECT:reconnect=0;atomic_store(&disconnect_requested,true);esp_wifi_disconnect();close_http();atomic_store(&state,SM_WIFI_IDLE);break;
    case SM_ECHO:memcpy(out->payload,r->payload,r->length);out->length=r->length;break;
    case SM_HTTP_OPEN: {
        close_http();
        if(atomic_load(&state)!=SM_WIFI_READY){out->result=SM_OFFLINE;break;}
        if(!require_size(r,sizeof(SmHttpOpen))){out->result=SM_INVALID;break;}
        SmHttpOpen open;memcpy(&open,r->payload,sizeof(open));
        if(!memchr(open.url,0,sizeof(open.url)) || !memchr(open.authorization,0,sizeof(open.authorization)) ||
           (strncmp(open.url,"http://",7) && strncmp(open.url,"https://",8)) ||
           strchr(open.authorization,'\r')||strchr(open.authorization,'\n')) {out->result=SM_INVALID;memset(&open,0,sizeof(open));break;}
        esp_http_client_config_t options={.url=open.url,.timeout_ms=5000,.disable_auto_redirect=true,
            .crt_bundle_attach=esp_crt_bundle_attach,.buffer_size=4096};
        http=esp_http_client_init(&options);
        if(!http){out->result=SM_IO;memset(&open,0,sizeof(open));break;}
        if(open.authorization[0])esp_http_client_set_header(http,"Authorization",open.authorization);
        esp_http_client_set_header(http,"Connection","close");
        esp_http_client_set_header(http,"User-Agent","PSPStreamer-StreamMaster/0.1");
        esp_err_t err=esp_http_client_open(http,0);
        int64_t length=err==ESP_OK?esp_http_client_fetch_headers(http):-1;
        if(length<0){out->result=SM_IO;close_http();}
        else {SmHttpResult result={esp_http_client_get_status_code(http),length>UINT32_MAX?UINT32_MAX:(uint32_t)length};
            memcpy(out->payload,&result,sizeof(result));out->length=sizeof(result);esp_http_client_set_timeout_ms(http,500);}
        memset(&open,0,sizeof(open));break;
    }
    case SM_HTTP_READ: {
        if(!http){out->result=SM_OFFLINE;break;}
        int n=esp_http_client_read(http,(char *)out->payload,SM_PAYLOAD_SIZE);
        if(n==-ESP_ERR_HTTP_EAGAIN)out->result=SM_BUSY;
        else if(n<0)out->result=SM_IO;
        else {out->length=n;if(!n && !esp_http_client_is_complete_data_received(http))out->result=SM_BUSY;}
        break;
    }
    case SM_HTTP_CLOSE:close_http();break;
    default:
        if(r->op>=SM_SOCKET_OPEN && r->op<=SM_SOCKET_RESET)sm_sockets_command(r,out);
        else out->result=SM_INVALID;
        break;
    }
done:sm_seal(out);
}

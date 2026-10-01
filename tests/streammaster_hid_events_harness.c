/* Actual callback with host substitutes for ESP APIs. */
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include "streammaster/gamepad.h"
#include "streammaster/gamepad_options.h"
enum {ESP_HIDH_INIT_EVT,ESP_HIDH_OPEN_EVT,ESP_HIDH_GET_DSCP_EVT,ESP_HIDH_DATA_IND_EVT,ESP_HIDH_CLOSE_EVT,ESP_HIDH_VC_UNPLUG_EVT};
enum {ESP_HIDH_CONN_STATE_CONNECTED,ESP_HIDH_CONN_STATE_CONNECTING,ESP_HIDH_CONN_STATE_DISCONNECTED,ESP_HIDH_CONN_STATE_DISCONNECTING};
#define ESP_HIDH_OK 0
#define ESP_BT_CONNECTABLE 1
#define ESP_BT_NON_DISCOVERABLE 0
#define ESP_HIDH_REPORT_MODE 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
typedef int esp_hidh_cb_event_t;
typedef struct {
    struct {int status;} init;
    struct {int status,conn_status;uint8_t handle,bd_addr[6];} open;
    struct {int status;uint8_t handle;const uint8_t *dsc_list;unsigned dl_len,vendor_id,product_id;} dscp;
    struct {int status;uint8_t handle;const uint8_t *data;unsigned len;} data_ind;
    struct {int status,conn_status;uint8_t handle;} close,unplug;
} esp_hidh_cb_param_t;
static int guard,active_handle=-1,disconnects,protocols,allow=1;
static atomic_int ready,online,accept_input;
static int64_t pair_until,last_input_log;
static uint8_t peer[6];
static char peer_name[48];
static uint8_t peer_battery;
static void esp_bt_gap_read_remote_name(const uint8_t *address){(void)address;}
static SmBtStatus status;
static SmPad pad;
static SmHidMap map;
static SmHidPad hidpad;
static SmBtProfile current_profile;
static unsigned current_home;
static void load_peer_profile_locked(const uint8_t address[6]){(void)address;current_profile=sm_bt_default_profile();}
static void refresh_bonds(int replace){(void)replace;}
static void esp_bt_gap_set_scan_mode(int c,int d){(void)c;(void)d;}
static void state(unsigned s,int err){status.state=s;status.error=err;}
static void neutral(void){pad.buttons=pad.raw_buttons=0;pad.connected=0;pad.x=pad.y=128;pad.sequence++;}
static int pairing_allowed(const uint8_t *addr){(void)addr;return allow;}
static void esp_bt_hid_host_disconnect(const uint8_t *addr){(void)addr;disconnects++;}
static void esp_bt_hid_host_set_protocol(const uint8_t *addr,int mode){(void)addr;(void)mode;protocols++;}
static int64_t esp_timer_get_time(void){return 10;}
/* HID_CALLBACK */
int main(void) {
    current_profile=sm_bt_default_profile();atomic_store(&online,1);
    esp_hidh_cb_param_t p={0};hid(ESP_HIDH_INIT_EVT,&p);
    assert(atomic_load(&ready) && active_handle==-1);
    const uint8_t descriptor[]={5,9,0x19,1,0x29,1,0x15,0,0x25,1,0x75,1,0x95,1,0x81,2};
    const uint8_t report[]={1};
    /* Fresh and cached reconnect use the same two OPEN notifications. */
    for(int repeat=0;repeat<3;repeat++) {
        state(SM_BT_CONNECTING,0);pair_until=30000000;
        p.open.status=0;p.open.conn_status=ESP_HIDH_CONN_STATE_CONNECTING;p.open.handle=255;
        hid(ESP_HIDH_OPEN_EVT,&p);
        assert(active_handle==-1 && status.state==SM_BT_CONNECTING && pair_until==30000000 && !disconnects);
        p.open.conn_status=ESP_HIDH_CONN_STATE_CONNECTED;p.open.handle=1;hid(ESP_HIDH_OPEN_EVT,&p);
        assert(active_handle==1 && status.state==SM_BT_CONNECTED && !pair_until && protocols==repeat+1);
        /* A duplicate request acknowledgement must not disconnect the real handle. */
        p.open.conn_status=ESP_HIDH_CONN_STATE_CONNECTING;p.open.handle=255;hid(ESP_HIDH_OPEN_EVT,&p);
        assert(active_handle==1 && !disconnects);
        p.dscp.handle=1;p.dscp.dsc_list=descriptor;p.dscp.dl_len=sizeof(descriptor);
        hid(ESP_HIDH_GET_DSCP_EVT,&p);assert(atomic_load(&accept_input));
        current_home=repeat==1?1:0;
        if(current_home)current_profile.binding[0]=0;
        p.data_ind.handle=1;p.data_ind.data=report;p.data_ind.len=1;hid(ESP_HIDH_DATA_IND_EVT,&p);
        assert(pad.connected && pad.buttons==(current_home?0x10000U:0x4000U) && status.reports==(unsigned)repeat+1);
        p.close.handle=2;p.close.conn_status=ESP_HIDH_CONN_STATE_DISCONNECTED;hid(ESP_HIDH_CLOSE_EVT,&p);
        assert(active_handle==1 && pad.connected);
        p.close.handle=1;p.close.conn_status=ESP_HIDH_CONN_STATE_DISCONNECTING;hid(ESP_HIDH_CLOSE_EVT,&p);
        assert(active_handle==1 && !pad.connected && !atomic_load(&accept_input));
        p.close.conn_status=ESP_HIDH_CONN_STATE_DISCONNECTED;hid(ESP_HIDH_CLOSE_EVT,&p);
        assert(active_handle==-1 && status.state==SM_BT_READY);
    }
    p.open.status=7;p.open.conn_status=ESP_HIDH_CONN_STATE_DISCONNECTED;p.open.handle=255;
    hid(ESP_HIDH_OPEN_EVT,&p);assert(status.state==SM_BT_ERROR && status.error==7 && active_handle==-1);
    puts("HID request/connected/descriptor/input/disconnecting/closed sequence OK");
}

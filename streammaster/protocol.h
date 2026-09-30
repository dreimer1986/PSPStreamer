/* SPDX-License-Identifier: GPL-2.0-or-later
 * StreamMaster v1: fixed-size little-endian USB records. No native pointers. */
#ifndef STREAMMASTER_PROTOCOL_H
#define STREAMMASTER_PROTOCOL_H
#include <stdint.h>
#include <string.h>
#define SM_VERSION 1
#define SM_MAGIC 0x31525453U
#define SM_FRAME_SIZE 4096
#define SM_PAYLOAD_SIZE (SM_FRAME_SIZE-32)
#define SM_USB_PID 0x5354 /* Private development PID on the PSP's Sony VID. */
#define SM_USB_SUBCLASS 0x53
#define SM_USB_PROTOCOL 0x01
enum {SM_INFO=1,SM_CONFIG_GET,SM_CONFIG_SET,SM_SCAN,SM_CONNECT,SM_DISCONNECT,SM_ECHO,SM_HTTP_OPEN,SM_HTTP_READ,SM_HTTP_CLOSE,SM_NETWORK_INFO,SM_CAPABILITIES};
#define SM_USB_METRICS 13U
enum {SM_PROFILES_GET=14,SM_PROFILE_SAVE,SM_PROFILE_SELECT,SM_PROFILE_DELETE,SM_PROFILE_AUTO};
#define SM_PROFILE_COUNT 5U
#define SM_CAP_COMPACT 1U
#define SM_CAP_BULK_PAIR 2U
#define SM_CAP_BULK_EXT 4U
#define SM_CAP_USB_METRICS 8U
#define SM_CAP_PROFILES 16U
#define SM_CAP_GAMEPAD 32U
#define SM_CAP_NET_DIAG 64U
#define SM_NET_DIAG 43U
typedef struct {
    uint32_t token,state,loops,rx_bytes,tx_bytes,again,heartbeat_ms,rx_age_ms;
    int32_t last_io,last_errno;
    uint32_t connect_ms,available;
} SmSocketDiag;
typedef struct {
    uint32_t wifi_state,disconnect_reason,internal_free,sampled;
    int32_t rssi;
    SmSocketDiag socket[6];
} SmNetDiag;
_Static_assert(sizeof(SmNetDiag)==308,"Network diagnostics layout");
#define SM_BT_STATUS 40U
#define SM_BT_ACTION 41U
#define SM_BT_USB_DIAG 42U
#define SM_BT_OPTIONS_GET 44U
#define SM_BT_OPTIONS_SET 45U
typedef struct {uint32_t reconnect,button[16];} SmBtOptions;
#define SM_BT_SETUP_GET 46U
#define SM_BT_SETUP_SET 47U
#define SM_BT_INPUT_GET 48U
/* Source 1..16 = HID button, 17..20 = hat up/right/down/left; zero = off. */
typedef struct {uint8_t binding[12],axis_x,axis_y,invert,configured;} SmBtProfile;
typedef struct {uint32_t version;uint8_t address[6],reserved[2];uint32_t session,reconnect;SmBtProfile profile;} SmBtSetup;
_Static_assert(sizeof(SmBtSetup)==36,"Controller setup wire layout");
/* Last enumeration probes, not a live device list. Never contains credentials. */
enum {SM_BT_PROBE_OPEN=1,SM_BT_PROBE_DESCRIPTOR,SM_BT_PROBE_FILTER,
      SM_BT_PROBE_CONFIG,SM_BT_PROBE_INTERFACE,SM_BT_PROBE_CLAIM,SM_BT_PROBE_STARTED,SM_BT_PROBE_WAIT_PSP};
typedef struct {uint32_t address;uint16_t vid,pid;uint32_t device_class,interface_class,endpoints,phase;int32_t result;uint32_t attempts;} SmBtUsbProbe;
typedef struct {uint32_t count,reserved;SmBtUsbProbe probe[8];} SmBtUsbDiag;
_Static_assert(sizeof(SmBtUsbDiag)==264,"Bluetooth USB diagnostics layout");
#define SM_DEV_GAMEPAD 0x53540030U
#define SM_PAD_MAGIC 0x31444150U
enum {SM_BT_NONE,SM_BT_STARTING,SM_BT_READY,SM_BT_SCANNING,SM_BT_CONNECTING,SM_BT_CONNECTED,SM_BT_ERROR};
enum {SM_BT_SCAN=1,SM_BT_PAIR,SM_BT_DISCONNECT,SM_BT_FORGET};
typedef struct {uint32_t magic,buttons,connected,sequence;uint8_t x,y;uint16_t raw_buttons;
    uint8_t axes[6],axes_valid,hat;uint32_t session;} SmPad;
typedef struct {uint8_t address[6];int8_t rssi;uint8_t reserved;char name[48];} SmBtDevice;
typedef struct {uint32_t state,count,reports;int32_t error;uint16_t vid,pid;uint8_t selected[6],reserved[2];SmBtDevice device[8];} SmBtStatus;
typedef struct {uint32_t action;uint8_t address[6];uint8_t reserved[2];} SmBtAction;
_Static_assert(sizeof(SmPad)==32,"Gamepad wire layout");
#define SM_BULK_FRAME_SIZE 8192U
#define SM_BULK_MAX_FRAME_SIZE 32768U
#define SM_BULK_MAX_DEPTH 4U
#define SM_BULK_PAYLOAD_SIZE (SM_BULK_FRAME_SIZE-32)
#define SM_PAIR_PAYLOAD_SIZE (2*SM_BULK_PAYLOAD_SIZE)
#define SM_MAX_GROUP_PAYLOAD (2*(SM_BULK_MAX_FRAME_SIZE-32))
#define SM_LEGACY_RESULT_SIZE (8+SM_PAIR_PAYLOAD_SIZE)
#define SM_COMPACT 2U
enum {SM_OK=0,SM_INVALID=-1,SM_OFFLINE=-2,SM_IO=-3,SM_TIMEOUT=-4,SM_BUSY=-5,SM_TLS=-6};
enum {SM_REPLY=1,SM_CFG_DHCP=1,SM_CFG_AUTO_DNS=2,SM_CFG_KEEP_PASSWORD=4,SM_CFG_HAS_PASSWORD=8};
enum {SM_WIFI_IDLE,SM_WIFI_CONNECTING,SM_WIFI_READY,SM_WIFI_FAILED};
enum {SM_DEV_START=0x53540001,SM_DEV_STOP,SM_DEV_STATUS,SM_DEV_EXCHANGE,SM_DEV_CANCEL,SM_DEV_EXCHANGE_COMPACT};
/* Local PSP driver ABI only; no change to the ESP wire protocol. */
enum {SM_DEV_READ_BEGIN=0x53540010,SM_DEV_READ_FINISH};
enum {SM_DEV_BULK_CAPS=0x53540020,SM_DEV_BULK_BEGIN,SM_DEV_BULK_FINISH};
#define SM_DEV_BULK_EXT_CAPS 0x53540023U
enum {SM_SOCKET_OPEN=20,SM_SOCKET_STATUS,SM_SOCKET_WRITE,SM_SOCKET_READ,SM_SOCKET_CLOSE,SM_SOCKET_RESET};
#define SM_SOCKET_READ_BULK 26U
#define SM_SOCKET_READ_BULK_EXT 27U
typedef struct {
    uint64_t requests,bytes,queue_us,work_us,reply_wait_us,copy_us,tx_us,gap_us;
    uint64_t checksum_us,ring_copy_us;
    uint32_t tx_max_us,queue_max_us;
} SmUsbMetrics;
_Static_assert(sizeof(SmUsbMetrics)==88,"USB metrics wire layout");
enum {SM_SOCKET_FREE,SM_SOCKET_CONNECTING,SM_SOCKET_READY,SM_SOCKET_EOF,SM_SOCKET_ERROR};
#define SM_SOCKET_COUNT 6
typedef struct {uint32_t token,port,tls;char host[128];} SmSocketOpen;
typedef struct {uint32_t token,length;} SmSocketRequest;
typedef struct {uint32_t state,available,space;int32_t error;} SmSocketStatus;
typedef struct {
    uint32_t magic,version,op,sequence,length;
    int32_t result;
    uint32_t flags,checksum;
    unsigned char payload[SM_PAYLOAD_SIZE];
} SmFrame;
typedef struct {
    uint32_t magic,version,op,sequence,length;
    int32_t result;
    uint32_t flags,checksum;
    unsigned char payload[SM_BULK_MAX_FRAME_SIZE-32];
} SmBulkFrame;
typedef struct {
    int32_t result;
    uint32_t length;
    unsigned char payload[SM_MAX_GROUP_PAYLOAD];
} SmBulkResult;
static inline uint32_t sm_bulk_checksum(const SmBulkFrame *f) {
    uint32_t h=2166136261U;const unsigned char *p=(const unsigned char *)f;
    for(int i=0;i<28;i++)h=(h^p[i])*16777619U;
    for(uint32_t i=0;i<f->length;i++)h=(h^f->payload[i])*16777619U;
    return h;
}
static inline int sm_bulk_valid(const SmBulkFrame *f) {
    unsigned limit=f->op==SM_SOCKET_READ_BULK?SM_BULK_PAYLOAD_SIZE:
        f->op==SM_SOCKET_READ_BULK_EXT?SM_BULK_MAX_FRAME_SIZE-32:0;
    return limit && f->magic==SM_MAGIC && f->version==SM_VERSION &&
        f->flags==SM_REPLY && f->length<=limit && f->checksum==sm_bulk_checksum(f);
}
static inline unsigned sm_bulk_wire_size(unsigned payload) {
    unsigned n=32+payload;
    return n<SM_BULK_FRAME_SIZE && !(n%64)?n+1:n;
}
static inline unsigned sm_bulk_wire_size_op(unsigned op,unsigned payload) {
    if(op==SM_SOCKET_READ_BULK)return sm_bulk_wire_size(payload);
    unsigned n=32+payload;
    return n<SM_BULK_MAX_FRAME_SIZE && !(n%64)?n+1:n;
}
_Static_assert(sizeof(SmBulkFrame)==SM_BULK_MAX_FRAME_SIZE,"Bulk frame layout");
typedef struct {
    uint32_t flags;
    char ssid[33],password[65];
    char ip[16],mask[16],gateway[16],dns[16],dns2[16];
    unsigned char reserved[2];
} SmConfig;
/* Passwords are cleared in replies; HAS_PASSWORD permits slot-local retention. */
typedef struct {
    uint32_t version,active,automatic;
    SmConfig slot[SM_PROFILE_COUNT];
} SmProfiles;
typedef struct {uint32_t slot;SmConfig config;} SmProfileSave;
_Static_assert(sizeof(SmProfiles)==932,"Profile storage and wire layout");
typedef struct {
    char firmware[32],ip[16],gateway[16],dns[16],dns2[16];
    uint32_t wifi_state,disconnect_reason,free_heap,usb_requests;
    int32_t rssi;
} SmInfo;
/* Separate command preserves the original SM_INFO layout for older apps. */
typedef struct {
    SmInfo info;
    char mask[16],ssid[33];
    unsigned char reserved[3];
    uint32_t config_flags;
} SmNetworkInfo;
typedef struct {char ssid[33];int8_t rssi;uint8_t channel,auth;} SmAccessPoint;
typedef struct {uint32_t count;SmAccessPoint ap[24];} SmScan;
typedef struct {char url[256],authorization[256];} SmHttpOpen;
typedef struct {int32_t status;uint32_t content_length;} SmHttpResult;
_Static_assert(sizeof(SmFrame)==SM_FRAME_SIZE,"USB frame layout");
_Static_assert(sizeof(SmConfig)==184,"Network config layout");
_Static_assert(sizeof(SmScan)<=SM_PAYLOAD_SIZE,"Scan fits one frame");
static inline uint32_t sm_checksum(const SmFrame *f) {
    uint32_t h=2166136261U;const unsigned char *p=(const unsigned char *)f;
    for(int i=0;i<28;i++)h=(h^p[i])*16777619U;
    for(uint32_t i=0;i<f->length;i++)h=(h^f->payload[i])*16777619U;
    return h;
}
static inline void sm_seal(SmFrame *f){f->magic=SM_MAGIC;f->version=SM_VERSION;f->checksum=sm_checksum(f);}
static inline int sm_valid(const SmFrame *f) {
    return f->magic==SM_MAGIC && f->version==SM_VERSION && f->length<=SM_PAYLOAD_SIZE &&
        f->flags<=SM_COMPACT && f->checksum==sm_checksum(f);
}
/* Full-speed short packet terminates a compact transfer. Avoid a trailing
 * full 64-byte packet (which would otherwise require a separate ZLP). */
static inline unsigned sm_wire_size(unsigned payload) {
    unsigned n=32+payload;
    if(n<SM_FRAME_SIZE && !(n%64))n++;
    return n;
}
static inline int sm_request_wire_valid(const SmFrame *f,unsigned received) {
    if(received<32 || f->length>SM_PAYLOAD_SIZE || (f->flags && f->flags!=SM_COMPACT))return 0;
    if(received!=(f->flags==SM_COMPACT?sm_wire_size(f->length):SM_FRAME_SIZE))return 0;
    return sm_valid(f);
}
static inline int sm_ip(const char *s,uint32_t *out) {
    uint32_t value=0;
    for(int part=0;part<4;part++) {
        unsigned n=0,digits=0;
        while(*s>='0' && *s<='9'){n=n*10+(*s++-'0');if(++digits>3 || n>255)return 0;}
        if(!digits || (part<3?*s++!='.':*s!=0))return 0;
        value=(value<<8)|n;
    }
    if(out)*out=value;
    return 1;
}
static inline int sm_unicast(uint32_t ip){return ip && (ip>>24)!=127 && (ip>>24)<224 && (ip>>24)!=0;}
static inline int sm_config_valid(const SmConfig *c,int allow_keep) {
    if(c->flags & ~(SM_CFG_DHCP|SM_CFG_AUTO_DNS|(allow_keep?SM_CFG_KEEP_PASSWORD:0)))return 0;
    if(!memchr(c->ssid,0,sizeof(c->ssid)) || !c->ssid[0] || !memchr(c->password,0,sizeof(c->password)))return 0;
    size_t n=strlen(c->password);
    if(n && n<8)return 0;
    if(n==64)for(size_t i=0;i<n;i++)if(!((c->password[i]>='0'&&c->password[i]<='9') ||
        (c->password[i]>='a'&&c->password[i]<='f') || (c->password[i]>='A'&&c->password[i]<='F')))return 0;
    const char *fields[]={c->ip,c->mask,c->gateway,c->dns,c->dns2};
    for(int i=0;i<5;i++)if(!memchr(fields[i],0,16))return 0;
    if(!(c->flags&SM_CFG_DHCP)) {
        uint32_t ip,mask,gw;
        if(!sm_ip(c->ip,&ip)||!sm_ip(c->mask,&mask)||!sm_ip(c->gateway,&gw))return 0;
        uint32_t inv=~mask;
        if(!sm_unicast(ip)||!mask||inv<3||(inv&(inv+1)) || !(ip&inv) || (ip&inv)==inv)return 0;
        if(gw && (!sm_unicast(gw)||(ip&mask)!=(gw&mask)||!(gw&inv)||(gw&inv)==inv || gw==ip))return 0;
        if(c->flags&SM_CFG_AUTO_DNS)return 0; /* no DHCP lease to supply DNS */
    }
    if(!(c->flags&SM_CFG_AUTO_DNS)) {
        uint32_t dns;
        if(!sm_ip(c->dns,&dns)||!sm_unicast(dns))return 0;
        if(c->dns2[0] && (!sm_ip(c->dns2,&dns)||!sm_unicast(dns)))return 0;
    }
    return 1;
}
#endif

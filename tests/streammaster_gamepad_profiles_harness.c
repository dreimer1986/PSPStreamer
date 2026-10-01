#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include "streammaster/gamepad_options.h"
#define ESP_OK 0
#define NVS_READWRITE 1
#define pdTRUE 1
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
typedef int esp_err_t;
typedef int nvs_handle_t;
static SmBtOptions options;
/* PROFILE_TYPES */
static int guard,actions,fail_commit;
static atomic_int reconnect_suspended,ready;
static SmBtStatus status;
static SmBtUsbDiag usb_diag;
static SmPad pad;
static uint8_t peer[6];
static ProfileStore disk_profiles,staged_profiles;
static SmBtOptions disk_options,staged_options;
static int known_peer(const uint8_t address[6]){return address[0]>=1 && address[0]<=4;}
static int sm_usb_psp_status(void){return 0;}
static int xQueueSend(int q,const void *a,int timeout){(void)q;(void)a;(void)timeout;return pdTRUE;}
static int nvs_open(const char *name,int mode,nvs_handle_t *out){(void)name;(void)mode;*out=1;staged_profiles=disk_profiles;staged_options=disk_options;return ESP_OK;}
static int nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t size){
    (void)h;if(!strcmp(key,"profiles_v1")){assert(size==sizeof(staged_profiles));memcpy(&staged_profiles,data,size);}
    else {assert(!strcmp(key,"options") && size==sizeof(staged_options));memcpy(&staged_options,data,size);}return ESP_OK;
}
static int nvs_commit(nvs_handle_t h){(void)h;if(fail_commit)return -1;disk_profiles=staged_profiles;disk_options=staged_options;return ESP_OK;}
static void nvs_close(nvs_handle_t h){(void)h;}
/* COMMAND */
static SmBtSetup get_setup(void) {
    SmFrame request={.op=SM_BT_SETUP_GET},reply={0};assert(sm_gamepad_command(&request,&reply)==0);
    SmBtSetup setup;assert(reply.length==sizeof(setup));memcpy(&setup,reply.payload,sizeof(setup));return setup;
}
static int save(SmBtSetup *setup) {
    SmFrame request={.op=SM_BT_SETUP_SET,.length=sizeof(*setup)},reply={0};memcpy(request.payload,setup,sizeof(*setup));
    return sm_gamepad_command(&request,&reply);
}
int main(void) {
    /* EP0 events lack raw inputs. Verify the complete learning snapshot. */
    pad=(SmPad){.magic=SM_PAD_MAGIC,.connected=1,.session=42,.raw_buttons=0x8001,
        .axes={1,2,3,4,5,6},.axes_valid=63,.hat=8};
    SmFrame raw_request={.op=SM_BT_INPUT_GET},raw_reply={0};
    assert(sm_gamepad_command(&raw_request,&raw_reply)==SM_OK);
    assert(raw_reply.length==sizeof(pad) && !memcmp(raw_reply.payload,&pad,sizeof(pad)));
    options=sm_bt_default_options();status.state=SM_BT_CONNECTED;
    for(unsigned i=1;i<=4;i++) {
        peer[0]=i;pad.session=i;load_peer_profile_locked(peer);
        SmBtSetup setup=get_setup();assert(!setup.profile.configured);
        assert(setup.version==2);setup.reserved[0]=i+10;
        setup.profile.binding[0]=i+4;setup.profile.axis_x=3;setup.profile.axis_y=4;setup.profile.configured=1;
        assert(!save(&setup));
    }
    /* Reboot-like reload: profiles still match addresses, not connection order. */
    memset(&profiles,0,sizeof(profiles));profiles=disk_profiles;options=disk_options;
    for(unsigned i=4;i>=1;i--){peer[0]=i;pad.session++;load_peer_profile_locked(peer);assert(current_profile.configured && current_profile.binding[0]==i+4 && current_profile.axis_x==3 && current_home==i+10);}
    SmBtSetup legacy=get_setup();legacy.version=1;legacy.reserved[0]=0;assert(!save(&legacy) && current_home==11);
    SmBtSetup bad_home=get_setup();bad_home.reserved[0]=21;assert(save(&bad_home)==SM_INVALID);
    SmBtSetup stale=get_setup();pad.session++;assert(save(&stale)==SM_OFFLINE);
    stale=get_setup();stale.address[0]=2;assert(save(&stale)==SM_OFFLINE);
    stale=get_setup();stale.profile.binding[0]=21;assert(save(&stale)==SM_INVALID);
    stale=get_setup();stale.profile.binding[0]=16;fail_commit=1;assert(save(&stale)==SM_IO && current_profile.binding[0]==5);fail_commit=0;
    peer[0]=5;load_peer_profile_locked(peer);stale=get_setup();assert(save(&stale)==SM_BUSY);
    puts("Per-address storage, four-slot bound, reconnect reload, stale session and failed save OK");
}

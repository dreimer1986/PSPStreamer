#pragma once
#include "protocol.h"
void sm_led_init(void);
void sm_led_usb(int attached);
unsigned sm_network_state(void);
void sm_sockets_init(void);
void sm_sockets_reset(void);
void sm_sockets_idle(void);
void sm_sockets_command(const SmFrame *request,SmFrame *reply);
void sm_network_drop_http(void);
void sm_network_init(void);
void sm_network_command(const SmFrame *request,SmFrame *reply);
void sm_network_idle(void);
void sm_usb_task(void *unused);
void sm_usb_daemon(void *unused);
void sm_sockets_bulk_read(const SmFrame *r,SmBulkFrame *out);
void sm_sockets_bulk_cost(uint32_t *copy,uint32_t *checksum);
void sm_usb_metrics_snapshot(SmUsbMetrics *out);
void sm_gamepad_init(void);
void sm_gamepad_snapshot(SmPad *out);
int sm_gamepad_command(const SmFrame *request,SmFrame *reply);

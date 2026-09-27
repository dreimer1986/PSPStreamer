#pragma once
#include "protocol.h"
void sm_led_init(void);
void sm_led_usb(int attached);
unsigned sm_network_state(void);
void sm_network_drop_http(void);
void sm_network_init(void);
void sm_network_command(const SmFrame *request,SmFrame *reply);
void sm_network_idle(void);
void sm_usb_task(void *unused);
void sm_usb_daemon(void *unused);

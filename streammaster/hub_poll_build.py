#!/usr/bin/env python3
"""Generate the experimental channel-saving IDF 5.5.1 hub variant.

The SDK checkout remains untouched. Only the Bluetooth build uses this file.
Upstream source retains its Apache-2.0 notices. Fail closed on SDK changes.
"""
import hashlib
import pathlib
import sys

source = pathlib.Path(sys.argv[1]).read_text()
if hashlib.sha256(source.encode()).hexdigest() != "220b704ebfb54c65e07624c556ae874d1ef48d1488a4d95c6cf745702cd8e06e":
    raise SystemExit("Unsupported ext_hub.c: review channel-saving changes for this SDK first")

def replace(old, new):
    global source
    if source.count(old) != 1:
        raise SystemExit(f"Hub patch anchor not unique: {old[:80]}")
    source = source.replace(old, new)

# Keep upstream request/completion, port debounce, reset and hotplug logic.
# Only replace the permanently allocated hub interrupt endpoint with periodic
# GET_STATUS requests on its already allocated control endpoint.
replace('    usbh_ep_handle_t ep_hdl;\n', '    usbh_ep_handle_t ep_hdl = NULL;\n')
replace('    ret = usbh_ep_alloc(config->dev_hdl, &ep_config, &ep_hdl);',
        '    (void)ep_config; /* Channel-saving hub: no interrupt pipe. */\n    ret = ESP_OK;')
replace('    if (is_active) {\n        ESP_LOGD',
        '    if (is_active && !ext_hub_dev->constant.ep_in_hdl) return ESP_OK;\n    if (is_active) {\n        ESP_LOGD')
replace('    ESP_ERROR_CHECK(usbh_ep_free(ext_hub_dev->constant.ep_in_hdl));',
        '    if (ext_hub_dev->constant.ep_in_hdl) ESP_ERROR_CHECK(usbh_ep_free(ext_hub_dev->constant.ep_in_hdl));')
for name in ('flush', 'dequeue', 'clear'):
    old = f'static void handle_ep1_{name}(ext_hub_dev_t *ext_hub_dev)\n{{'
    replace(old, old + '\n    if (!ext_hub_dev->constant.ep_in_hdl) return;')

source += '''

/* StreamMaster addition; GPL-2.0-or-later. Called only by the USB library
 * daemon, never by a timer ISR or another client. No object survives this call.
 * One port per 50 ms; no synthetic connection events, actual status is read.
 */
void sm_usb_hub_poll(void)
{
    static TickType_t last;
    static unsigned cursor;
    TickType_t now = xTaskGetTickCount();
    if ((TickType_t)(now - last) < pdMS_TO_TICKS(50) || !p_ext_hub_driver) return;
    last = now;
    ext_port_hdl_t port = NULL;
    EXT_HUB_ENTER_CRITICAL();
    ext_hub_dev_t *hub;
    TAILQ_FOREACH(hub, &p_ext_hub_driver->dynamic.ext_hubs_tailq, dynamic.tailq_entry) {
        if (hub->single_thread.state != EXT_HUB_STATE_CONFIGURED ||
            hub->single_thread.stage != EXT_HUB_STAGE_IDLE ||
            hub->dynamic.flags.waiting_release || hub->dynamic.flags.is_gone ||
            !hub->single_thread.maxchild) continue;
        port = hub->constant.ports[cursor++ % hub->single_thread.maxchild];
        break;
    }
    EXT_HUB_EXIT_CRITICAL();
    if (port) p_ext_hub_driver->constant.port_driver->get_status(port);
}
'''
source = '#include "freertos/FreeRTOS.h"\n#include "freertos/task.h"\n' + source
pathlib.Path(sys.argv[2]).write_text(source)

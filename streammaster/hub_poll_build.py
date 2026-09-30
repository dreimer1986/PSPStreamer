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
replace('        ext_hub_stage_t stage;                      /**< Device\'s stage */',
        '        bool sm_poll_ready; /* Set only when all port handling completed. */\n'
        '        bool sm_poll_inflight; /* Raw probe, not a port-state-machine request. */\n'
        '        ext_hub_stage_t stage;                      /**< Device\'s stage */')
replace('    if (is_active) {\n        ESP_LOGD',
        '    if (is_active && !ext_hub_dev->constant.ep_in_hdl) {\n'
        '        ext_hub_dev->single_thread.sm_poll_ready = true;\n'
        '        return ESP_OK;\n    }\n    if (is_active) {\n        ESP_LOGD')
replace('    ESP_ERROR_CHECK(usbh_ep_free(ext_hub_dev->constant.ep_in_hdl));',
        '    if (ext_hub_dev->constant.ep_in_hdl) ESP_ERROR_CHECK(usbh_ep_free(ext_hub_dev->constant.ep_in_hdl));')
for name in ('flush', 'dequeue', 'clear'):
    old = f'static void handle_ep1_{name}(ext_hub_dev_t *ext_hub_dev)\n{{'
    replace(old, old + '\n    if (!ext_hub_dev->constant.ep_in_hdl) return;')

# The normal port state machine assumes GET_STATUS was requested because an
# interrupt reported a CHANGE, or as part of its own reset/power sequence.
# Never feed unchanged polling snapshots into it: ENABLED otherwise emits a
# second RESET_COMPLETED, and a queued GET_STATUS can displace a pending RESET.
replace('static esp_err_t ext_hub_control_request(ext_hub_dev_t *ext_hub_dev, uint8_t port1, uint8_t request, uint8_t feature)\n{',
        'static esp_err_t ext_hub_control_request(ext_hub_dev_t *ext_hub_dev, uint8_t port1, uint8_t request, uint8_t feature)\n{\n'
        '    ext_hub_dev->single_thread.sm_poll_ready = false; /* Reset/feature sequence owns EP0 until complete. */')
replace('    // [TODO: IDF-12174] Revisit the External Hub Driver to ensure consistent error handling.\n'
        '    ESP_ERROR_CHECK(p_ext_hub_driver->constant.port_driver->set_status(ext_hub_dev->constant.ports[port_idx], new_status));',
        '''    if (ext_hub_dev->single_thread.sm_poll_inflight) {
        ext_hub_dev->single_thread.sm_poll_inflight = false;
        if (new_status->wPortChange.val) {
            // Equivalent to one bit in the original interrupt bitmap. Let the
            // port driver request its own status and manage its status_lock.
            ESP_ERROR_CHECK(p_ext_hub_driver->constant.port_driver->get_status(ext_hub_dev->constant.ports[port_idx]));
        } else {
            ext_hub_dev->single_thread.sm_poll_ready = true;
        }
        return;
    }
    // Original reset/power/status requests retain their unmodified path.
    ESP_ERROR_CHECK(p_ext_hub_driver->constant.port_driver->set_status(ext_hub_dev->constant.ports[port_idx], new_status));''')

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
    ext_hub_dev_t *selected = NULL;
    uint8_t port1 = 0;
    EXT_HUB_ENTER_CRITICAL();
    ext_hub_dev_t *hub;
    TAILQ_FOREACH(hub, &p_ext_hub_driver->dynamic.ext_hubs_tailq, dynamic.tailq_entry) {
        if (hub->single_thread.state != EXT_HUB_STATE_CONFIGURED ||
            hub->single_thread.stage != EXT_HUB_STAGE_IDLE ||
            !hub->single_thread.sm_poll_ready || hub->single_thread.sm_poll_inflight ||
            hub->dynamic.flags.waiting_release || hub->dynamic.flags.is_gone ||
            !hub->single_thread.maxchild) continue;
        selected = hub;
        port1 = 1 + cursor++ % hub->single_thread.maxchild;
        hub->single_thread.sm_poll_ready = false;
        hub->single_thread.sm_poll_inflight = true;
        break;
    }
    EXT_HUB_EXIT_CRITICAL();
    // No port-driver action is queued until this raw probe sees a change.
    // This daemon owns both the hub list and its shared EP0 control buffer.
    if (selected) ext_hub_control_request(selected, port1, USB_B_REQUEST_HUB_GET_PORT_STATUS, 0);
}
'''
source = '#include "freertos/FreeRTOS.h"\n#include "freertos/task.h"\n' + source
pathlib.Path(sys.argv[2]).write_text(source)

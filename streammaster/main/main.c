/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "bridge.h"
#include "board.h"
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "usb/usb_host.h"
void app_main(void) {
#if !SM_GENERIC_BOARD
    /* Onju V3 MAX98357A shutdown. Never repurpose audio/touch GPIOs for USB. */
    gpio_set_direction(GPIO_NUM_21,GPIO_MODE_OUTPUT);gpio_set_level(GPIO_NUM_21,0);
#endif
    sm_led_init();
    sm_network_init();
    sm_sockets_init();
    usb_host_config_t config={.skip_phy_setup=false,.intr_flags=ESP_INTR_FLAG_LEVEL1};
    ESP_ERROR_CHECK(usb_host_install(&config));
    if(xTaskCreate(sm_usb_daemon,"usb-events",4096,NULL,12,NULL)!=pdPASS ||
       xTaskCreate(sm_usb_task,"streammaster",12288,NULL,8,NULL)!=pdPASS)abort();
}

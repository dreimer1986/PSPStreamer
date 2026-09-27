/* SPDX-License-Identifier: GPL-2.0-or-later
 * Onju V3: six 800-kHz GRB pixels on GPIO11, matching its board definition.
 * The whole frame fits in RMT RAM: no timing-sensitive refill interrupts. */
#include "bridge.h"
#include "led_pattern.h"
#include <stdatomic.h>
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static atomic_bool usb_attached;
static rmt_symbol_word_t symbols[SM_LED_COUNT*24+1];
void sm_led_usb(int attached){atomic_store(&usb_attached,attached!=0);}
static void led_task(void *unused) {
    (void)unused;
    rmt_channel_handle_t channel=NULL;rmt_encoder_handle_t encoder=NULL;
    rmt_tx_channel_config_t cfg={.gpio_num=11,.clk_src=RMT_CLK_SRC_DEFAULT,
        .resolution_hz=20000000,.mem_block_symbols=192,.trans_queue_depth=1};
    rmt_copy_encoder_config_t copy={};
    esp_err_t rc=rmt_new_tx_channel(&cfg,&channel);
    if(rc==ESP_OK)rc=rmt_new_copy_encoder(&copy,&encoder);
    if(rc==ESP_OK)rc=rmt_enable(channel);
    if(rc!=ESP_OK) {
        ESP_LOGW("leds","LED init disabled: %s",esp_err_to_name(rc));
        if(encoder)rmt_del_encoder(encoder);
        if(channel)rmt_del_channel(channel);
        vTaskDelete(NULL);return;
    }
    uint8_t previous[6][3];memset(previous,255,sizeof(previous));
    int bars=0;unsigned phase=0;
    for(;;) {
        unsigned state=sm_network_state();
        if(state!=SM_WIFI_READY)bars=0;
        else if(!bars || phase%4==0) {
            wifi_ap_record_t ap;
            if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK)bars=sm_led_bars(ap.rssi,bars);
            else bars=0;
        }
        uint8_t rgb[6][3];sm_led_pattern(state,bars,atomic_load(&usb_attached),phase++,rgb);
        if(memcmp(rgb,previous,sizeof(rgb))) {
            unsigned n=0;
            for(int led=0;led<6;led++) {
                const uint8_t grb[]={rgb[led][1],rgb[led][0],rgb[led][2]};
                for(int c=0;c<3;c++)for(int bit=7;bit>=0;bit--) {
                    int one=(grb[c]>>bit)&1;
                    /* 1.25 us bit period: 0=0.35/0.90, 1=0.70/0.55 us. */
                    symbols[n++]=(rmt_symbol_word_t){.level0=1,.duration0=one?14:7,.level1=0,.duration1=one?11:18};
                }
            }
            symbols[n]=(rmt_symbol_word_t){.level0=0,.duration0=3000,.level1=0,.duration1=3000};
            rmt_transmit_config_t tx={0};
            rc=rmt_transmit(channel,encoder,symbols,sizeof(symbols),&tx);
            if(rc==ESP_OK)rc=rmt_tx_wait_all_done(channel,100);
            if(rc!=ESP_OK) {
                /* Nonessential display failure must not reset networking.
                 * Keep static payload/handles alive if hardware is still busy. */
                ESP_LOGW("leds","LED updates stopped: %s",esp_err_to_name(rc));
                vTaskDelete(NULL);return;
            }
            memcpy(previous,rgb,sizeof(rgb));
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
void sm_led_init(void) {
    if(xTaskCreate(led_task,"status-leds",3072,NULL,1,NULL)!=pdPASS)
        ESP_LOGW("leds","No memory for status LEDs; bridge remains enabled");
}

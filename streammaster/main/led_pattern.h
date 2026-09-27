/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "../protocol.h"
#define SM_LED_COUNT 6
/* Four central LEDs; 3 dB downward hysteresis prevents threshold flicker. */
static inline int sm_led_bars(int rssi,int previous) {
    static const int threshold[]={-80,-70,-60};
    int bars=1;
    while(bars<4 && rssi>=threshold[bars-1])bars++;
    if(previous>=1 && previous<=4 && bars<previous) {
        bars=previous;
        while(bars>1 && rssi<threshold[bars-2]-3)bars--;
    }
    return bars;
}
/* RGB brightness intentionally limited to 24/255. Outer LEDs are separate
 * from signal strength; this shows link state, not Internet reachability. */
static inline void sm_led_pattern(unsigned state,int bars,int usb,unsigned phase,uint8_t rgb[6][3]) {
    memset(rgb,0,18);rgb[0][0]=rgb[0][1]=rgb[0][2]=8;
    if(usb){rgb[5][1]=8;rgb[5][2]=24;}
    if(state==SM_WIFI_READY) {
        if(bars<0)bars=0;
        if(bars>4)bars=4;
        for(int i=1;i<=bars;i++){rgb[i][0]=bars<=2?20:0;rgb[i][1]=bars==1?8:24;}
    } else if(state==SM_WIFI_CONNECTING) {
        unsigned led=1+phase%4;rgb[led][0]=24;rgb[led][1]=12;
    } else if(state==SM_WIFI_FAILED) {
        for(int i=1;i<=4;i++)rgb[i][0]=(phase/2)%2?3:24;
    } else for(int i=1;i<=4;i++)rgb[i][2]=3;
}

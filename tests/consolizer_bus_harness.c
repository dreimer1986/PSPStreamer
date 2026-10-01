#include <assert.h>
#include <stddef.h>
#define PSP_USBBUS_DRIVERNAME "USBBusDriver"
static int state=2,starts,stops,start_result,stop_result;
static int sceUsbGetDrvState(const char *name){(void)name;return state;}
static int sceUsbStart(const char *name,int size,void *args){
    (void)name;(void)size;(void)args;starts++;
    if(start_result>=0 || (unsigned)start_result==0x80243001U)state=1;
    return start_result;
}
static int sceUsbStop(const char *name,int size,void *args){
    (void)name;(void)size;(void)args;stops++;
    if(stop_result>=0)state=2;
    return stop_result;
}
#include "psp-client/streammaster_usb/bus_owner.h"
#include "psp-controller/report_config.h"
int main(void){
    state=1;assert(sm_bus_start()==0 && !sm_bus_owned && !starts);
    sm_bus_stop();assert(!stops && state==1);
    state=2;assert(sm_bus_start()==0 && sm_bus_owned && starts==1);
    assert(sm_bus_start()==0 && starts==1 && sm_bus_owned);
    sm_bus_stop();assert(!sm_bus_owned && stops==1 && state==2);
    start_result=-99;assert(sm_bus_start()==-99 && !sm_bus_owned);
    sm_bus_stop();assert(stops==1);
    start_result=(int)0x80243001U;
    assert(sm_bus_start()==0 && !sm_bus_owned);sm_bus_stop();assert(stops==1);
    state=2;start_result=0;assert(!sm_bus_start());stop_result=-1;
    sm_bus_stop();assert(sm_bus_owned);stop_result=0;sm_bus_stop();assert(!sm_bus_owned);
    assert(consolizer_report_setting("")==1);
    assert(consolizer_report_setting("# report=0\nreport=1\n")==1);
    assert(consolizer_report_setting("enabled=1\r\n report=0 \r\n")==0);
    assert(consolizer_report_setting("report=0")==0);
    assert(consolizer_report_setting("report=0\nreport=1")==1);
    assert(consolizer_report_setting("report=00\nreport=\nreport=2")==1);
    return 0;
}

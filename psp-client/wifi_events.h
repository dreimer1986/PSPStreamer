/* APCTL invokes the handler on its own thread: never perform file I/O,
 * formatting, waits or network calls there. One producer, serialized drains. */
#define WIFI_EVENT_CAPACITY 64U
typedef struct {
    unsigned long long tick;
    int old_state,new_state,event,error;
} WifiEvent;
static WifiEvent wifi_events[WIFI_EVENT_CAPACITY];
static volatile unsigned int wifi_event_write,wifi_event_read,wifi_event_dropped;
static volatile int wifi_event_drain_lock;
static unsigned int wifi_event_reported_drops;
static int wifi_event_handler=-1;
static void wifi_event_callback(int old_state,int new_state,int event,int error,void *arg) {
    (void)arg;
    if(!debug_enabled)return;
    unsigned int write=wifi_event_write;
    __sync_synchronize();
    if(write-wifi_event_read>=WIFI_EVENT_CAPACITY) {wifi_event_dropped++;return;}
    wifi_events[write%WIFI_EVENT_CAPACITY]=(WifiEvent){
        sceKernelGetSystemTimeWide(),old_state,new_state,event,error};
    __sync_synchronize();wifi_event_write=write+1;
}
static void wifi_events_register(void) {
    if(wifi_event_handler>=0)return;
    recovery_log("APCTL handler begin",0,0,"event diagnostics");
    wifi_event_handler=sceNetApctlAddHandler(wifi_event_callback,NULL);
    recovery_log("APCTL handler registered",wifi_event_handler,0,"event diagnostics");
    /* Registration failure must not change association behavior. */
}
static void wifi_events_terminated(void) {
    /* Called only AFTER successful Term: the old APCTL handlers are gone. */
    wifi_event_handler=-1;
}
static void wifi_events_drain(void) {
    if(wifi_event_read==wifi_event_write && wifi_event_dropped==wifi_event_reported_drops)return;
    if(__sync_lock_test_and_set(&wifi_event_drain_lock,1))return;
    static const char *const names[]={"connect request","scan request","scan complete",
        "established","got IP","disconnect request","error","info","EAP auth",
        "key exchange","reconnect"};
    unsigned int end=wifi_event_write;
    __sync_synchronize();
    if(end-wifi_event_read>4)end=wifi_event_read+4;
    while(wifi_event_read!=end) {
        WifiEvent event=wifi_events[wifi_event_read%WIFI_EVENT_CAPACITY];
        __sync_synchronize();wifi_event_read++;
        char stage[160];
        snprintf(stage,sizeof(stage),"event_ms=%llu old=%d new=%d error=0x%08X %s",
            event.tick/1000ULL,event.old_state,event.new_state,(unsigned int)event.error,
            event.event>=0 && event.event<11?names[event.event]:"unknown event");
        recovery_log("APCTL event",event.error,event.event,stage);
    }
    unsigned int dropped=wifi_event_dropped;
    if(dropped!=wifi_event_reported_drops) {
        recovery_log("APCTL events dropped",(int)(dropped-wifi_event_reported_drops),0,"diagnostic queue full");
        wifi_event_reported_drops=dropped;
    }
    __sync_lock_release(&wifi_event_drain_lock);
}

/* SPDX-License-Identifier: GPL-2.0-or-later
 * No formatting, heap allocation or disk writes in USB/audio callbacks.
 * HTTP is opt-in, read-only, carries counters only and survives USB loss.
 */
#include "trace.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_app_desc.h"
#include <stdio.h>
#include <stdatomic.h>
static portMUX_TYPE guard=portMUX_INITIALIZER_UNLOCKED;
static SmTrace trace;
static httpd_handle_t server;
static atomic_int http_error;
void sm_trace_event(unsigned kind,unsigned a,unsigned b) {
    unsigned ms=(unsigned)(esp_timer_get_time()/1000);
    portENTER_CRITICAL(&guard);
    sm_trace_record(&trace,ms,kind,a,b);
    portEXIT_CRITICAL(&guard);
}
void sm_trace_command_begin(unsigned op) {
    if(op==SM_DIAGNOSTICS)return;
    unsigned ms=(unsigned)(esp_timer_get_time()/1000);
    portENTER_CRITICAL(&guard);
    trace.commands++;trace.last_op=op;trace.last_command_ms=ms;
    portEXIT_CRITICAL(&guard);
}
void sm_trace_command(unsigned op,int result,unsigned duration) {
    if(op==SM_DIAGNOSTICS)return;
    portENTER_CRITICAL(&guard);
    if(duration>trace.max_command_us)trace.max_command_us=duration;
    portEXIT_CRITICAL(&guard);
    if(result<0 && result!=SM_BUSY)sm_trace_event(SM_TRACE_COMMAND_ERROR,op,(unsigned)result);
}
static void snapshot(SmTrace *out) {
    portENTER_CRITICAL(&guard);*out=trace;portEXIT_CRITICAL(&guard);
    out->version=1;out->uptime_ms=(unsigned)(esp_timer_get_time()/1000);
    out->reset_reason=esp_reset_reason();
    out->internal_free=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    out->internal_largest=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    out->http_error=atomic_load(&http_error);
    sm_trace_audio(out);
}
static esp_err_t get_trace(httpd_req_t *req) {
    SmTrace out;snapshot(&out);char line[768];
    httpd_resp_set_type(req,"application/json");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    int n=snprintf(line,sizeof(line),"{\"firmware\":\"%s\",\"uptime_ms\":%lu,\"reset_reason\":%lu,\"free\":%lu,\"largest\":%lu,\"commands\":%lu,\"last_op\":%lu,\"last_command_ms\":%lu,\"max_command_us\":%lu,\"usb_events\":%lu,\"bt_state\":%lu,\"audio_session\":%lu,\"rate\":%lu,\"flags\":%lu,\"queued\":%lu,\"completed\":%lu,\"underruns\":%lu,\"dma_callbacks\":%lu,\"max_dma_gap_us\":%lu,\"event_count\":%lu,\"events\":[",
        esp_app_get_description()->version,(unsigned long)out.uptime_ms,(unsigned long)out.reset_reason,
        (unsigned long)out.internal_free,(unsigned long)out.internal_largest,(unsigned long)out.commands,
        (unsigned long)out.last_op,(unsigned long)out.last_command_ms,(unsigned long)out.max_command_us,
        (unsigned long)out.usb_events,(unsigned long)out.bt_state,(unsigned long)out.audio_session,
        (unsigned long)out.audio_rate,(unsigned long)out.audio_flags,(unsigned long)out.audio_queued,
        (unsigned long)out.audio_completed,(unsigned long)out.audio_underruns,(unsigned long)out.dma_callbacks,
        (unsigned long)out.max_dma_gap_us,(unsigned long)out.event_count);
    if(n<0 || n>=(int)sizeof(line))return ESP_FAIL;
    if(httpd_resp_send_chunk(req,line,n)!=ESP_OK)return ESP_FAIL;
    unsigned count=out.event_count<SM_TRACE_EVENTS?out.event_count:SM_TRACE_EVENTS;
    for(unsigned i=0;i<count;i++) {
        SmTraceEvent *e=&out.events[(out.event_count-count+i)%SM_TRACE_EVENTS];
        n=snprintf(line,sizeof(line),"%s{\"ms\":%lu,\"kind\":%lu,\"a\":%lu,\"b\":%lu}",i?",":"",
            (unsigned long)e->ms,(unsigned long)e->kind,(unsigned long)e->a,(unsigned long)e->b);
        if(httpd_resp_send_chunk(req,line,n)!=ESP_OK)return ESP_FAIL;
    }
    n=snprintf(line,sizeof(line),"],\"http_error\":%ld}",(long)out.http_error);
    if(httpd_resp_send_chunk(req,line,n)!=ESP_OK)return ESP_FAIL;
    return httpd_resp_send_chunk(req,NULL,0);
}
int sm_trace_command_read(const SmFrame *r,SmFrame *reply) {
    uint32_t action=0;
    if(r->length!=sizeof(action))return SM_INVALID;
    memcpy(&action,r->payload,sizeof(action));
    if(action>2)return SM_INVALID;
    if(action==2 && server){httpd_stop(server);server=NULL;}
    if(action==1 && !server &&
       (heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<49152 ||
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<12288)) {
        /* Audio/USB allocations take priority. USB diagnostics still work. */
        atomic_store(&http_error,ESP_ERR_NO_MEM);
    } else if(action==1 && !server) {
        httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();
        cfg.server_port=8080;cfg.max_open_sockets=1;cfg.lru_purge_enable=true;
        cfg.max_uri_handlers=1;cfg.max_resp_headers=1;
        cfg.recv_wait_timeout=2;cfg.send_wait_timeout=2;cfg.stack_size=4096;
        int err=httpd_start(&server,&cfg);
        if(err==ESP_OK) {
            httpd_uri_t uri={.uri="/diagnostics.json",.method=HTTP_GET,.handler=get_trace};
            err=httpd_register_uri_handler(server,&uri);
            if(err==ESP_OK && heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<24576)err=ESP_ERR_NO_MEM;
            if(err!=ESP_OK){httpd_stop(server);server=NULL;}
        }
        atomic_store(&http_error,err);
    }
    SmTrace out;snapshot(&out);memcpy(reply->payload,&out,sizeof(out));reply->length=sizeof(out);
    return SM_OK;
}

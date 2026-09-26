#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned int SceSize;
static int remote_control_sequence, create_failure, start_failure;
static int server_https;
static int deleted, joined, index_reply, count_reply, count_events;
static int events[16], seconds_seen[16], cancel_during_http, fail_http_once;
static const char *replies[16];
static int remote_control_running, remote_control_action, remote_control_seek_seconds;
static int music_remote_worker(SceSize args, void *argp);
static void sceKernelDelayThread(int us);
/* JSON_FUNCTIONS */
static int sceKernelCreateThread(const char *name, int (*worker)(SceSize, void *),
                                int priority, int stack, int flags, void *option) {
    assert(!strcmp(name, "PSPStreamerMusicRemote") && worker == music_remote_worker);
    assert(priority == (server_https?0x41:0x40));
    assert(stack == 0x10000 && flags == 0 && !option);
    return create_failure ? -10 : 123;
}
static int sceKernelStartThread(int id, int args, void *argp) {
    assert(id == 123 && args == 0 && !argp); return start_failure ? -11 : 0;
}
static void sceKernelDeleteThread(int id) { assert(id == 123); deleted++; }
static void sceKernelWaitThreadEnd(int id, void *timeout) {
    assert(id == 123 && !timeout); joined++;
}
static int remote_http_get(const char *path, char *reply, int capacity, volatile int *running);
#define ID_SIZE 512
static float current_duration_seconds;
static int stream_start_seconds;
static int stop_reports;
static int remote_http_get_budget(const char *path, char *reply, int capacity, volatile int *running, int budget) {
    (void)reply;(void)capacity;assert(*running && budget==1500);
    assert(strstr(path,"state=stopped"));stop_reports++;return 0;
}
/* PLEX_REPORTING */
#include "remote_state.h"
static void network_worker_finished(const char *name){(void)name;}
#include "music_remote.h"
static void subtitle_page_prefetch(unsigned long long *retry) {(void)retry;}
/* VIDEO_REMOTE_WORKER */
static int remote_http_get(const char *path, char *reply, int capacity, volatile int *running) {
    int after = -1;
    assert(sscanf(path, "/api/remote/next?after=%d", &after) == 1);
    assert(after == remote_control_sequence);
    if (cancel_during_http) *running = 0;
    if (fail_http_once) { fail_http_once = 0; return -1; }
    assert(index_reply < count_reply);
    snprintf(reply, capacity, "%s", replies[index_reply++]); return (int)strlen(reply);
}
static void sceKernelDelayThread(int us) {
    assert(us == 10000 || us == 500000);
    if (music_remote_action) {
        events[count_events] = music_remote_action;
        seconds_seen[count_events++] = music_remote_seconds;
        music_remote_action = MUSIC_REMOTE_NONE;
    }
    if (index_reply == count_reply) music_remote_running = remote_control_running = 0;
}
static void reset(void) {
    create_failure = start_failure = cancel_during_http = fail_http_once = 0;
    deleted = joined = index_reply = count_events = 0;
    remote_control_sequence = 10;
    remote_session[0] = 0;
    remote_control_action = 0;
    remote_control_seek_seconds = -1;
}
int main(int argc,char **argv) {
    (void)argv;
    char report_path[1024];
    current_duration_seconds=240;stream_start_seconds=15;
    plex_report_begin("plex.42.p8.0.0123456789ab");
    plex_report_path(report_path,sizeof(report_path),10,0);
    assert(!strstr(report_path,"&plex=")); /* Do not mark buffering as playback. */
    plex_started=1;plex_position_ms=17001;
    plex_report_path(report_path,sizeof(report_path),10,0);
    assert(strstr(report_path,"position=17001&duration=240000"));
    plex_paused=1;plex_report_path(report_path,sizeof(report_path),10,0);
    assert(strstr(report_path,"state=paused"));
    plex_report_stop(10);assert(stop_reports==1);
    plex_report_begin("filesystem-id");plex_report_stop(10);assert(stop_reports==2);
    plex_report_begin("jellyfin.aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.0123456789ab");
    plex_started=1;plex_report_path(report_path,sizeof(report_path),10,0);
    assert(strstr(report_path,"&plex=jellyfin."));
    plex_report_begin("filesystem-id");
    plex_paused=1;plex_report_path(report_path,sizeof(report_path),10,0);
    assert(strstr(report_path,"state=paused") && strstr(report_path,"&media=filesystem-id"));
    assert(strstr(report_path,"position=15000&duration=240000") && strstr(report_path,"started=0"));
    plex_paused=0;plex_report_path(report_path,sizeof(report_path),10,0);
    assert(strstr(report_path,"state=playing") && strstr(report_path,"&media=filesystem-id"));
    server_https=argc>1;
    reset();
    strcpy(remote_session,"old");
    int seq=10;
    assert(remote_state_reset("{\"seq\":10,\"session\":\"new\"}",&seq) && seq==0);
    seq=10;
    assert(remote_state_reset("{\"seq\":1}",&seq) && seq==0);
    reset();
    create_failure = 1;
    assert(music_remote_start() == -10 && !music_remote_running);
    music_remote_stop(); assert(!joined && !deleted);
    reset(); start_failure = 1;
    assert(music_remote_start() == -11 && !music_remote_running);
    music_remote_stop(); assert(!joined && deleted == 1);
    reset();
    replies[0] = "{\"seq\":11,\"action\":\"pause\"}";
    replies[1] = "{\"seq\":11,\"action\":\"pause\"}"; /* duplicate */
    replies[2] = "{\"seq\":12,\"action\":\"resume\"}";
    replies[3] = "{\"seq\":13,\"action\":\"seek\",\"seconds\":75}";
    replies[4] = "{\"seq\":14,\"action\":\"seek\",\"seconds\":-1}";
    replies[5] = "{\"seq\":15,\"action\":\"stop\"}";
    count_reply = 6; fail_http_once = 1;
    assert(music_remote_start() == 0);
    music_remote_worker(0, NULL);
    assert(count_events == 4);
    assert(events[0] == MUSIC_REMOTE_PAUSE && events[1] == MUSIC_REMOTE_RESUME);
    assert(events[2] == MUSIC_REMOTE_SEEK && seconds_seen[2] == 75);
    assert(events[3] == MUSIC_REMOTE_STOP && remote_control_sequence == 15);
    music_remote_stop(); assert(joined == 1 && deleted == 1);
    reset();
    replies[0] = "{\"seq\":11,\"action\":\"play\",\"id\":\"next\",\"kind\":\"audio\"}";
    count_reply = 1;
    assert(music_remote_start() == 0); music_remote_worker(0, NULL);
    assert(music_remote_action == MUSIC_REMOTE_PLAY && remote_control_sequence == 10);
    music_remote_stop(); assert(joined == 1 && deleted == 1);
    reset(); cancel_during_http = 1;
    replies[0] = "{\"seq\":11,\"action\":\"pause\"}";
    count_reply = 1;
    assert(music_remote_start() == 0); music_remote_worker(0, NULL);
    assert(music_remote_action == MUSIC_REMOTE_NONE && remote_control_sequence == 10);
    music_remote_stop();
    reset();
    replies[0] = "{\"seq\":11,\"action\":\"play\",\"id\":\"next-video\",\"kind\":\"video\"}";
    count_reply = 1; remote_control_running = 1;
    remote_control_thread(0, NULL);
    assert(remote_control_action == 4 && remote_control_sequence == 10);
    reset();
    replies[0] = "{\"seq\":11,\"action\":\"pause\"}";
    count_reply = 1; remote_control_running = 1;
    remote_control_thread(0, NULL);
    assert(remote_control_action == 1 && remote_control_sequence == 11);
    reset();
    strcpy(music_radio_id,"radio.0123456789abcdef0123456789abcdef");
    music_radio_ready=0;
    replies[0]="{\"seq\":10,\"action\":\"idle\",\"radio_station\":\"MiniDisc Player\",\"radio_title\":\"Björk - Song\"}";
    count_reply=1;
    assert(music_remote_start()==0);music_remote_worker(0,NULL);
    assert(music_radio_ready && !strcmp(music_radio_station,"MiniDisc Player"));
    assert(!strcmp(music_radio_title,"Björk - Song"));
    assert(remote_control_sequence==10 && music_remote_action==MUSIC_REMOTE_NONE);
    music_remote_stop();music_radio_id[0]=0;music_radio_ready=0;
    return 0;
}

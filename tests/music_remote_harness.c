#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned int SceSize;
static int remote_control_sequence, create_failure, start_failure;
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
    assert(priority > 0x3D && stack >= 0x4000 && flags == 0 && !option);
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
#include "remote_state.h"
#include "music_remote.h"
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
int main(void) {
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
    return 0;
}

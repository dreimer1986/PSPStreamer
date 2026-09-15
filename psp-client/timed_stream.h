/* Single FLV reader and bounded compressed-packet queues.
 * Included in main.c after network helpers. The codec workers keep their
 * existing firmware APIs and receive complete packets with container PTS.
 */
#include "flv.h"
typedef struct {
    unsigned char *data;
    int size, pts;
} TimedPacket;
typedef struct {
    TimedPacket slots[128];
    unsigned int read, write;
    SceUID free, ready;
} TimedQueue;
static TimedQueue timed_video, timed_audio;
static volatile int timed_active, timed_running, timed_eof, timed_error, timed_playing;
static volatile int timed_has_audio, timed_audio_done;
static volatile int timed_socket = -1;
static int timed_reader_id = -1;
static int timed_video_origin, timed_video_origin_set, timed_position_ms;
static char timed_request[2048];
#include "stream_diagnostic.h"

static int timed_wait(SceUID sema) {
    SceUInt timeout = 10000;
    return sceKernelWaitSema(sema, 1, &timeout) >= 0;
}
static int timed_queue_init(TimedQueue *q) {
    memset(q, 0, sizeof(*q));
    q->ready = -1;
    q->free = sceKernelCreateSema("PTSFree", 0, 128, 128, NULL);
    if (q->free < 0) return -1;
    q->ready = sceKernelCreateSema("PTSReady", 0, 0, 128, NULL);
    return q->ready < 0 ? -1 : 0;
}
static void timed_queue_destroy(TimedQueue *q) {
    unsigned int i;
    for (i = 0; i < 128; i++) { free(q->slots[i].data); q->slots[i].data = NULL; }
    if (q->free >= 0) sceKernelDeleteSema(q->free);
    if (q->ready >= 0) sceKernelDeleteSema(q->ready);
    q->free = q->ready = -1;
}
static int timed_put(TimedQueue *q, const unsigned char *data, int size, int pts) {
    TimedPacket *p;
    while (timed_running && !timed_wait(q->free)) {}
    if (!timed_running) return -1;
    p = &q->slots[q->write % 128];
    p->data = memalign(64, (size + 63) & ~63);
    if (!p->data) { stream_diag.reason="packet allocation failed"; sceKernelSignalSema(q->free, 1); return -1; }
    memcpy(p->data, data, size);
    p->size = size; p->pts = pts;
    q->write++;
    sceKernelSignalSema(q->ready, 1);
    return 0;
}
static int timed_get(TimedQueue *q, TimedPacket *out) {
    TimedPacket *p;
    if (!timed_wait(q->ready)) return 0;
    p = &q->slots[q->read % 128];
    *out = *p; p->data = NULL;
    q->read++;
    sceKernelSignalSema(q->free, 1);
    return 1;
}
/* Exact reads tolerate TCP fragmentation. A timeout returns to this worker,
 * never blocks the UI; cancellation closes the socket before joining it.
 * Startup/subtitle preparation and intentional pause have separate budgets.
 */
static int timed_read(unsigned char *out, int size) {
    int have = 0;
    stream_diag.wanted=size; stream_diag.received=0;
    unsigned long long last = sceKernelGetSystemTimeWide();
    while (have < size && timed_running) {
        int got = timed_recv(out + have, size - have);
        if (got == -2) {
            /* Preserve partial FLV packets across temporary delivery gaps.
             * Cancellation still polls every 100 ms; startup is unchanged. */
            unsigned long long limit = timed_playing ? 30000000ULL : 180000000ULL;
            if (playback_paused) last = sceKernelGetSystemTimeWide();
            if (sceKernelGetSystemTimeWide() - last < limit) continue;
            stream_diag.reason="read inactivity timeout";
            return -1;
        }
        if (got <= 0) return have ? -1 : 0;
        have += got; last = sceKernelGetSystemTimeWide();
        stream_diag.received=have;
    }
    return have == size ? 1 : -1;
}
static int timed_connect(int fd, struct sockaddr_in *server) {
    int nonblock = 1, status;
    unsigned long long start = sceKernelGetSystemTimeWide();
    if (sceNetInetSetsockopt(fd, SOL_SOCKET, SO_NONBLOCK, &nonblock, sizeof(nonblock)) < 0) {
        stream_diag.reason="nonblocking socket setup"; stream_diag.socket_error=sceNetInetGetErrno(); return -1;
    }
    status = sceNetInetConnect(fd, (struct sockaddr *)server, sizeof(*server));
    while (status < 0 && timed_running) {
        struct SceNetInetPollfd p = {fd, SCE_NET_INET_POLLOUT, 0};
        int ready = sceNetInetPoll(&p, 1, 100);
        if (ready < 0 || sceKernelGetSystemTimeWide() - start > 15000000ULL) {
            stream_diag.reason=ready<0?"connect poll error":"connect timeout";
            if(ready<0) stream_diag.socket_error=sceNetInetGetErrno();
            return -1;
        }
        if (ready) {
            int error = 0;
            socklen_t length = sizeof(error);
            if (!(p.revents & SCE_NET_INET_POLLOUT) ||
                sceNetInetGetsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0 || error) {
                stream_diag.reason="connect failed";
                stream_diag.socket_error=error?error:sceNetInetGetErrno(); return -1;
            }
            status = 0;
        }
    }
    if (!timed_running) return -1;
    nonblock = 0;
    return sceNetInetSetsockopt(fd, SOL_SOCKET, SO_NONBLOCK, &nonblock, sizeof(nonblock));
}
static int timed_reader(SceSize args, void *argp) {
    struct sockaddr_in server;
    unsigned char h[13], tag[11], previous[4];
    unsigned char *body = NULL, *annexb = NULL;
    char http[4096];
    FlvAvc avc;
    int n = 0, result = -1, fd, got, saw_end = 0;
    (void)args; (void)argp;
    stream_diag_reset();
    memset(&avc, 0, sizeof(avc));
    fd = sceNetInetSocket(AF_INET, SOCK_STREAM, 0);
    timed_socket = fd;
    if (fd < 0) { stream_diag.socket_error=sceNetInetGetErrno(); goto end; }
    stream_diag.stage="resolve server";
    if (prepare_server(&server) < 0) goto end;
    stream_diag.stage="connect";
    if (!timed_running || timed_connect(fd, &server) < 0) goto end;
    stream_diag.stage="send HTTP request";
    {
        int sent = 0, length = strlen(timed_request);
        while (sent < length && timed_running) {
            got = sceNetInetSend(fd, timed_request + sent, length - sent, 0);
            if (got <= 0) { stream_diag.socket_error=sceNetInetGetErrno(); goto end; }
            sent += got;
        }
    }
    /* Reading only the HTTP header bytes avoids consuming FLV data twice. */
    stream_diag.stage="HTTP header";
    while (n < (int)sizeof(http) - 1) {
        if (timed_read((unsigned char *)http + n, 1) != 1) goto end;
        http[++n] = 0;
        if (n >= 4 && !memcmp(http + n - 4, "\r\n\r\n", 4)) break;
    }
    if (!strstr(http, " 200 ") || n == (int)sizeof(http) - 1) goto end;
    stream_diag.stage="FLV header";
    if (timed_read(h, 13) != 1 || memcmp(h, "FLV\1", 4) ||
        flv_u32(h + 5) != 9 || flv_u32(h + 9) != 0 || !(h[4] & 1)) goto end;
    timed_has_audio = !!(h[4] & 4);
    body = malloc(FLV_MAX_VIDEO);
    annexb = malloc(FLV_MAX_VIDEO);
    if (!body || !annexb) { stream_diag.reason="reader allocation failed"; goto end; }
    while (timed_running) {
        unsigned int size, type;
        int pts, converted;
        stream_diag.stage="FLV tag header";
        got = timed_read(tag, 11);
        if (!got) { result = saw_end ? 0 : -1; break; }
        if (got < 0) break;
        size = flv_u24(tag + 1); type = tag[0];
        if (!size || size > FLV_MAX_VIDEO || flv_u24(tag + 8)) break;
        stream_diag.stage="FLV tag body";
        if (timed_read(body, size) != 1) break;
        stream_diag.stage="FLV tag trailer";
        if (timed_read(previous, 4) != 1 || flv_u32(previous) != size + 11) break;
        pts = (int)(flv_u24(tag + 4) | ((unsigned int)tag[7] << 24));
        if (type == 8) {
            stream_diag.stage="MP3 tag"; stream_diag.audio_pts=pts;
            if (size < 5 || size - 1 > FLV_MAX_AUDIO || (body[0] >> 4) != 2) break;
            if (timed_put(&timed_audio, body + 1, size - 1, pts) < 0) break;
        } else if (type == 9) {
            stream_diag.stage="AVC tag"; stream_diag.video_pts=pts;
            if (size < 5 || (body[0] & 15) != 7) break;
            if (body[1] == 0) {
                if (flv_config(&avc, body + 5, size - 5) < 0) break;
            } else if (body[1] == 1) {
                converted = flv_annexb(&avc, body + 5, size - 5, annexb, FLV_MAX_VIDEO);
                if (converted < 0) break;
                if (timed_put(&timed_video, annexb, converted, flv_pts(tag, body)) < 0) break;
            } else if (body[1] == 2) saw_end = 1;
            else break;
        } else if (type != 18) break;
    }
end:
    if(result<0 && timed_running) {
        stream_diag.failure_ms=(unsigned int)(sceKernelGetSystemTimeWide()/1000ULL);
        stream_diag.ap_result=sceNetApctlGetState(&stream_diag.ap_state);
    }
    free(body); free(annexb);
    if (timed_socket == fd) { timed_socket = -1; if (fd >= 0) sceNetInetClose(fd); }
    if (result < 0 && timed_running) timed_error = -1320;
    timed_eof = 1;
    return 0;
}

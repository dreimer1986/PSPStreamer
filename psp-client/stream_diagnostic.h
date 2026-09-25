#define DEBUG_DIAG(statement) do { if(debug_enabled) { statement } } while(0)
/* Reader-owned RAM snapshot. Save after joining the reader, not on audio or
 * video ticks. No deadlines, packet handling or playback clocks are changed. */
static struct {
    const char *stage, *reason;
    int socket_error, poll_result, poll_events, recv_result;
    int wanted, received, video_pts, audio_pts, ap_state, ap_result;
    unsigned int bytes, last_data_ms, failure_ms;
} stream_diag;

static void stream_diag_reset(void) {
    if(!debug_enabled) return;
    memset(&stream_diag,0,sizeof(stream_diag));
    stream_diag.stage="socket"; stream_diag.reason="invalid FLV or reader setup";
    stream_diag.video_pts=stream_diag.audio_pts=-1;
    stream_diag.ap_state=-1;
}

/* Preserve errors that the shared legacy stream_recv helper intentionally
 * collapses to EOF. Only the FLV reader uses this diagnostic wrapper. */
static int timed_recv(unsigned char *data,int size) {
    if(server_https) {
        int n=tls_recv(timed_socket,data,size,100);
        DEBUG_DIAG(stream_diag.recv_result=n;);
        if(n>0) {DEBUG_DIAG(stream_diag.bytes+=n;);DEBUG_DIAG(stream_diag.last_data_ms=(unsigned int)(sceKernelGetSystemTimeWide()/1000ULL););}
        else if(n!=-2) {DEBUG_DIAG(stream_diag.reason=n?"TLS receive error":"TLS EOF";);DEBUG_DIAG(stream_diag.socket_error=n;);}
        return n;
    }
    struct SceNetInetPollfd p={timed_socket,SCE_NET_INET_POLLIN,0};
    int result=sceNetInetPoll(&p,1,100);
    DEBUG_DIAG(stream_diag.poll_result=result;); DEBUG_DIAG(stream_diag.poll_events=p.revents;);
    if(!result) return -2;
    if(result<0) {
        DEBUG_DIAG(stream_diag.socket_error=sceNetInetGetErrno(););
        DEBUG_DIAG(stream_diag.reason="poll error";); return 0;
    }
    if(!(p.revents&SCE_NET_INET_POLLIN)) {
        if(debug_enabled) {
            int error=0; socklen_t length=sizeof(error);
            if(sceNetInetGetsockopt(timed_socket,SOL_SOCKET,SO_ERROR,&error,&length)<0)
                error=sceNetInetGetErrno();
            stream_diag.socket_error=error;
            stream_diag.reason="poll without readable data";
        }
        return 0;
    }
    result=sceNetInetRecv(timed_socket,data,size,0);
    if(result<0 && sceNetInetGetErrno()==35)return -2;
    DEBUG_DIAG(stream_diag.recv_result=result;);
    if(result<0) {
        DEBUG_DIAG(stream_diag.socket_error=sceNetInetGetErrno(););
        DEBUG_DIAG(stream_diag.reason="recv error";);
    } else if(!result) DEBUG_DIAG(stream_diag.reason="TCP EOF";);
    else {
        DEBUG_DIAG(stream_diag.bytes+=result;);
        DEBUG_DIAG(stream_diag.last_data_ms=(unsigned int)(sceKernelGetSystemTimeWide()/1000ULL););
    }
    return result;
}

static int stream_diag_save(int result) {
    if(!debug_enabled) return 0;
    char text[1024];
    int size=snprintf(text,sizeof(text),
        "result=%d stage=%s reason=%s\n"
        "errno=%d poll=%d revents=0x%X recv=%d wanted=%d received=%d\n"
        "bytes=%u video_pts_ms=%d audio_pts_ms=%d last_data_ms=%u failure_ms=%u gap_ms=%u\n"
        "apctl_result=%d apctl_state=%d\n",
        result,stream_diag.stage,stream_diag.reason,stream_diag.socket_error,
        stream_diag.poll_result,stream_diag.poll_events,stream_diag.recv_result,
        stream_diag.wanted,stream_diag.received,stream_diag.bytes,
        stream_diag.video_pts,stream_diag.audio_pts,stream_diag.last_data_ms,
        stream_diag.failure_ms,stream_diag.last_data_ms?stream_diag.failure_ms-stream_diag.last_data_ms:0,
        stream_diag.ap_result,stream_diag.ap_state);
    SceUID fd=sceIoOpen("ms0:/PSP/SYSTEM/PSPStreamer-stream-error.txt",
        PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);
    if(fd<0) return fd;
    int written=sceIoWrite(fd,text,size), closed=sceIoClose(fd);
    return written==size?closed:written<0?written:-5;
}

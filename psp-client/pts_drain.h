/* Liveness when the one demux reader is blocked on video, not on the network.
 * Keep audio PTS authoritative whenever a new PCM block is submitted. Never
 * infer EOF here: the reader must still reach the real FLV end marker. */
typedef struct {
    int active, audio_pts, origin;
    unsigned long long tick;
} PtsDrainClock;
static int pts_drain_update(PtsDrainClock *clock,unsigned long long now,
    int started,int audio_pts,int audio_end,int video_blocked,int audio_waiting,
    int packets_empty,int pcm_empty) {
    if(clock->active && audio_pts!=clock->audio_pts) {clock->active=0;return 0;}
    if(!clock->active && started && video_blocked && audio_waiting && packets_empty && pcm_empty) {
        clock->active=1;clock->audio_pts=audio_pts;clock->origin=audio_end;clock->tick=now;
    }
    return clock->active;
}
static int pts_drain_time(const PtsDrainClock *clock,unsigned long long now) {
    return clock->origin+(int)((now-clock->tick)/1000ULL);
}

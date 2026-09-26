#include <assert.h>
#include <stdio.h>
#include <string.h>

static int resume_pending,seek_requested,playback_reached_end,video_file_direction;
static int stream_start_seconds,scenario,calls,first_calls,next_calls,metadata_calls;
static int remote_next_audio,remote_next_track,remote_next_subtitle;
static int comfort_play_video(const char *id) {
    assert(++calls<8);
    playback_reached_end=video_file_direction=0;
    if(!strcmp(id,"first")) {
        first_calls++;
        int seeks=scenario==2?2:scenario?1:0;
        if(first_calls<=seeks) {
            stream_start_seconds=1000+first_calls*10;
            resume_pending=seek_requested=1;
            return 10;
        }
        if(scenario)assert(stream_start_seconds==1000+seeks*10);
        if(scenario==3)return 10; /* User Stop: never autoplay. */
        if(scenario==4){resume_pending=1;return -1320;} /* Network failure. */
    } else assert(!strcmp(id,"second") && stream_start_seconds==0);
    playback_reached_end=1;
    return 100;
}
static int comfort_play_audio(const char *id,const char *title) {
    (void)title;
    return comfort_play_video(id); /* Also exercise the shared music seek branch. */
}
static void sceKernelDelayThread(int delay) {(void)delay;}
static int load_media_metadata(const char *id) {
    assert(!strcmp(id,"second"));metadata_calls++;return 0;
}
static int remote_next_media(char *id,size_t capacity,int audio,int direction) {
    (void)audio;
    assert(capacity>=7 && direction==1);
    next_calls++;
    if(!strcmp(id,"second"))return 0;
    assert(!strcmp(id,"first"));strcpy(id,"second");
    remote_next_audio=!audio;remote_next_track=1;remote_next_subtitle=0;
    return 1;
}
int main(void) {
    for(scenario=0;scenario<6;scenario++) {
        int result=0,remote_is_audio=scenario==5,remote_audio=1,remote_subtitle=0;
        int selected_audio_track=0,selected_subtitle_track=-1;
        char remote_media_id[32]="first",current_media_name[32]="";
        const char *video_step="";
        resume_pending=seek_requested=playback_reached_end=video_file_direction=0;
        stream_start_seconds=calls=first_calls=next_calls=metadata_calls=0;
        /* REMOTE_PLAYBACK_LOOP */
        (void)video_step;
        if(scenario==3 || scenario==4) {
            assert(next_calls==0 && metadata_calls==0);
            assert((result<0)==(scenario==4));
        } else {
            assert(next_calls==2 && metadata_calls==1);
            assert(!strcmp(remote_media_id,"second"));
            assert(selected_audio_track==remote_audio && selected_subtitle_track==remote_subtitle);
            assert(!resume_pending && !seek_requested);
        }
    }
    puts("Remote autoplay: natural EOF, seek, repeated seek, Stop, error and music passed");
    return 0;
}

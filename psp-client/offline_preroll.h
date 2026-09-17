/* Called after reading AVC configuration, on the already-open local file.
 * Walk FLV PreviousTagSize links; do not scan/decode from the movie start.
 * Preload a full MP3 reservoir plus two complete frames for synthesis state.
 * Existing seek.idx files remain valid video-keyframe indexes. */
static int offline_preroll_read(SceUID fd,unsigned int offset,unsigned char *p,int size) {
    if(sceIoLseek(fd,(SceOff)offset,PSP_SEEK_SET)!=(SceOff)offset)return -1;
    int have=0;
    while(have<size && timed_running) {
        int n=sceIoRead(fd,p+have,size-have);
        if(n<=0)return -1;
        have+=n;
    }
    return have==size?0:-1;
}
static int offline_preroll_start(SceUID fd,unsigned int target,unsigned int *start) {
    unsigned int at=target;
    int frames=0,history=0;
    unsigned char prev[4],tag[11],body[9];
    *start=target;
    if(target<13)return -1;
    while(at>13 && timed_running) {
        if(offline_preroll_read(fd,at-4,prev,4)<0)return -1;
        unsigned int length=flv_u32(prev);
        if(length<11 || length>FLV_MAX_VIDEO+11 || at<17 || length>at-17)return -1;
        at-=length+4;
        if(offline_preroll_read(fd,at,tag,11)<0 || flv_u24(tag+1)+11!=length || flv_u24(tag+8))return -1;
        if(tag[0]!=8)continue;
        unsigned int size=length-11;
        int back,bytes;
        if(size<9 || size-1>FLV_MAX_AUDIO || offline_preroll_read(fd,at+11,body,9)<0 ||
           (body[0]>>4)!=2 || mp3_reservoir_info(body+1,size-1,&back,&bytes)<0)return -1;
        *start=at;
        /* The two frames nearest the target rebuild filter overlap; earlier
         * payload bytes supply their maximum possible reservoir dependency. */
        if(++frames>2)history+=bytes;
        if(history>=511)return 0;
    }
    return timed_running?0:-1;
}

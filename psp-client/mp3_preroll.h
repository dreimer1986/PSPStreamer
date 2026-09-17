#ifndef PSP_STREAMER_MP3_PREROLL_H
#define PSP_STREAMER_MP3_PREROLL_H
/* MPEG-1 Layer III side information. main_data_begin addresses at most 511
 * bytes in preceding frames, excluding headers/CRC/side information. */
static inline int mp3_reservoir_info(const unsigned char *p,int size,int *back,int *bytes) {
    if(size<6 || p[0]!=255 || (p[1]&0xfe)!=0xfa)return -1;
    int side=(p[3]>>6)==3?17:32;
    int header=4+((p[1]&1)?0:2);
    if(size<header+side)return -1;
    *back=(p[header]<<1)|(p[header+1]>>7);
    *bytes=size-header-side;
    return 0;
}
typedef struct {int bytes;} Mp3Preroll;
/* Called only during a local-seek warmup. An underflow is expected only when
 * the actual bitstream asks for history we have not fed yet. Other errors
 * remain fatal, even during preroll. */
static inline int mp3_preroll_missing(Mp3Preroll *state,const unsigned char *p,int size) {
    int back,bytes;
    if(mp3_reservoir_info(p,size,&back,&bytes)<0)return -1;
    int missing=back>state->bytes;
    state->bytes+=bytes;
    if(state->bytes>511)state->bytes=511;
    return missing;
}
#endif

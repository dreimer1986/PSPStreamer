/* Host check of the same low-level codecs used by the XBE. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#define PL_MPEG_IMPLEMENTATION
#include "../xbox-client/vendor/pl_mpeg.h"
int main(int argc,char **argv) {
    if(argc!=2)return 1;
    FILE *f=fopen(argv[1],"rb");if(!f)return 2;
    plm_buffer_t *v=plm_buffer_create_with_capacity(524288),*a=plm_buffer_create_with_capacity(4096);
    plm_video_t *vd=plm_video_create_with_buffer(v,1);plm_video_set_no_delay(vd,1);
    plm_audio_t *ad=plm_audio_create_with_buffer(a,1);
    unsigned vc=0,ac=0,pending=0;uint8_t h[16];
    while(fread(h,1,16,f)==16) {
        unsigned n;memcpy(&n,h+4,4);if(n>262144)return 3;
        if(h[0]=='E')break;
        unsigned char *data=malloc(n);if(fread(data,1,n,f)!=n)return 4;
        if(h[0]=='A') {plm_buffer_write(a,data,n);if(!plm_audio_decode(ad)||plm_audio_get_samplerate(ad)!=48000)return 5;ac++;}
        else {plm_buffer_write(v,data,n);pending++;if(pending>=2){if(!plm_video_decode(vd))return 6;vc++;pending--;}}
        free(data);
    }
    plm_buffer_signal_end(v);
    while(pending--){if(!plm_video_decode(vd))return 7;vc++;}
    printf("%u %u\n",vc,ac);
    plm_video_destroy(vd);plm_audio_destroy(ad);fclose(f);return 0;
}

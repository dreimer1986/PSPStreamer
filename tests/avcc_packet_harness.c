#include "avcc_packet.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
int main(void) {
    unsigned char config[]={1,77,0,30,255,225,0,2,0x67,42,1,0,2,0x68,17};
    AvcConfig c={0};
    assert(!avcc_config_parse(&c,config,sizeof(config)));
    assert(c.length_size==4 && c.sps_size==2 && c.pps_size==2);
    AvcConfig saved=c;
    for(int n=0;n<(int)sizeof(config);n++) {
        assert(avcc_config_parse(&c,config,n)<0);
        assert(!memcmp(&c,&saved,sizeof(c))); /* Atomic failed config parse. */
    }
    config[4]=254;assert(avcc_config_parse(&c,config,sizeof(config))<0);config[4]=255;
    unsigned char expected[]={0,0,0,4,0x65,0,0,1,0,0,0,2,6,123};
    for(int width=1;width<=4;width*=2) {
        unsigned char body[64],out[64];int n=0;
        const unsigned char *units[]={(unsigned char[]){9,0},(unsigned char[]){0x67,7},
            (unsigned char[]){0x68,8},expected+4,expected+12};
        const int sizes[]={2,2,2,4,2};
        c=saved;c.length_size=width;
        for(int k=0;k<5;k++) {
            for(int j=width-1;j>=0;j--)body[n++]=(unsigned char)(sizes[k]>>(j*8));
            memcpy(body+n,units[k],sizes[k]);n+=sizes[k];
        }
        assert(avcc_walk(&c,body,n,NULL)==(int)sizeof(expected));
        memset(out,0xaa,sizeof(out));assert(avcc_walk(&c,body,n,out)==(int)sizeof(expected));
        assert(!memcmp(out,expected,sizeof(expected)) && out[sizeof(expected)]==0xaa);
        assert(c.sps[1]==7 && c.pps[1]==8);
        assert(avcc_walk(&c,body,n-1,NULL)<0);
        unsigned char bad[]={255,255,255,255};assert(avcc_walk(&c,bad,4,NULL)<0);
    }
    /* Bounds at exactly the payload limit, then a further NAL. */
    unsigned char *large=calloc(1,AVC_MAX_DATA);assert(large);
    c=saved;large[1]=3;large[2]=255;large[3]=252;large[4]=0x65;
    assert(avcc_walk(&c,large,AVC_MAX_DATA,NULL)==AVC_MAX_DATA);
    large[3]=247;large[AVC_MAX_DATA-2]=1;large[AVC_MAX_DATA-1]=6;
    assert(avcc_walk(&c,large,AVC_MAX_DATA,NULL)==AVC_MAX_DATA);
    large[0]=128;assert(avcc_walk(&c,large,AVC_MAX_DATA,NULL)<0);free(large);
    AvcPacket *packet=aligned_alloc(64,640);assert(packet);
    memset(packet,0,640);packet->config=saved;packet->data_size=sizeof(expected);
    memcpy(packet->data,expected,sizeof(expected));
    assert(!((uintptr_t)packet->data&63));
    assert(avcc_packet_valid(packet,sizeof(*packet)+sizeof(expected)));
    assert(!avcc_packet_valid(packet,sizeof(*packet)+sizeof(expected)-1));
    memset(&c,0,sizeof(c));assert(packet->config.sps[1]==42);
    assert(!memcmp(packet->data,expected,sizeof(expected)));free(packet);
    puts("AVCC: 1/2/4-byte lengths, parameter snapshots, AUD filtering, malformed and maximum packets OK");
}

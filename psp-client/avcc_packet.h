/* SPDX-License-Identifier: GPL-2.0-or-later
 * Owned Media Engine access units. No reader-buffer pointers cross the queue.
 * Normalize NAL lengths once while copying from the FLV tag, not via Annex B.
 */
#ifndef PSP_STREAMER_AVCC_PACKET_H
#define PSP_STREAMER_AVCC_PACKET_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define AVC_MAX_DATA (256 * 1024)
typedef struct {
    unsigned char sps[256],pps[256];
    int sps_size,pps_size,length_size;
} AvcConfig;
typedef struct {
    AvcConfig config;
    unsigned int data_size;
    unsigned char reserved[48];
    unsigned char data[];
} AvcPacket;
_Static_assert(offsetof(AvcPacket,data)==576,"AVC payload must be 64-byte aligned");
static inline int avcc_config_valid(const AvcConfig *c) {
    return c->sps_size>0 && c->sps_size<=256 && c->pps_size>0 && c->pps_size<=256 &&
        (c->length_size==1 || c->length_size==2 || c->length_size==4);
}
static inline int avcc_set_parameter(AvcConfig *c,const unsigned char *p,int n,int type) {
    if(n<=0 || n>256 || (p[0]&31)!=type || (p[0]&128))return -1;
    if(type==7){memcpy(c->sps,p,n);c->sps_size=n;}
    else {memcpy(c->pps,p,n);c->pps_size=n;}
    return 0;
}
static inline int avcc_config_parse(AvcConfig *c,const unsigned char *p,int n) {
    AvcConfig next={0};int at=6,count;
    if(n<7 || p[0]!=1)return -1;
    next.length_size=(p[4]&3)+1;
    count=p[5]&31;
    for(int group=0;group<2;group++) {
        for(int i=0;i<count;i++) {
            if(n-at<2)return -1;
            int size=(p[at]<<8)|p[at+1];at+=2;
            if(size>n-at || avcc_set_parameter(&next,p+at,size,group?8:7)<0)return -1;
            at+=size;
        }
        if(!group){if(at>=n)return -1;count=p[at++];}
    }
    if(!avcc_config_valid(&next))return -1;
    *c=next;return 0;
}
/* First pass validates/sizes and snapshots in-band SPS/PPS. Second pass writes
 * straight into the owned aligned packet. SPS/PPS go to the firmware's separate
 * fields; AUD is omitted just as in the established decoder path. */
static inline int avcc_walk(AvcConfig *c,const unsigned char *p,int n,unsigned char *out) {
    int at=0,used=0;
    if(!avcc_config_valid(c) || n<=0 || n>AVC_MAX_DATA)return -1;
    while(at<n) {
        unsigned int size=0;
        if(n-at<c->length_size)return -1;
        for(int i=0;i<c->length_size;i++)size=(size<<8)|p[at++];
        if(!size || size>(unsigned int)(n-at) || (p[at]&128))return -1;
        int type=p[at]&31;
        if(!type)return -1;
        if(type==7 || type==8) {
            if(avcc_set_parameter(c,p+at,(int)size,type)<0)return -1;
        } else if(type!=9) {
            if(used>AVC_MAX_DATA-4 || size>(unsigned int)(AVC_MAX_DATA-used-4))return -1;
            if(out) {
                out[used]=(unsigned char)(size>>24);out[used+1]=(unsigned char)(size>>16);
                out[used+2]=(unsigned char)(size>>8);out[used+3]=(unsigned char)size;
                memcpy(out+used+4,p+at,size);
            }
            used+=4+(int)size;
        }
        at+=(int)size;
    }
    return used;
}
static inline int avcc_packet_valid(const AvcPacket *p,int bytes) {
    return bytes>=(int)sizeof(*p) && avcc_config_valid(&p->config) &&
        p->data_size>0 && p->data_size<=AVC_MAX_DATA &&
        p->data_size==(unsigned int)(bytes-(int)sizeof(*p));
}
#endif

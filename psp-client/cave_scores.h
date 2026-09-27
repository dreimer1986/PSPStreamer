/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PSPSTREAMER_CAVE_SCORES_H
#define PSPSTREAMER_CAVE_SCORES_H
#include "cave_game.h"
#include <stdio.h>
/* Two alternating, checksummed slots: never truncate the last good scores.
 * A power loss during a write leaves the preceding generation recoverable. */
typedef struct {
    CaveScore rows[CAVE_HALL_SIZE];
    int count,slot;
    uint32_t generation;
} CaveHall;
static inline uint32_t cave_scores_hash(const uint32_t *words,int count) {
    uint32_t hash=2166136261U;
    for(int i=0;i<count;i++)hash=(hash^words[i])*16777619U;
    return hash;
}
static inline int cave_scores_read(const char *path,CaveHall *out) {
    uint32_t words[35];FILE *f=fopen(path,"rb");if(!f)return 0;
    int valid=fread(words,sizeof(words),1,f)==1;
    if(valid)valid=fgetc(f)==EOF && !ferror(f);
    fclose(f);
    if(!valid || words[0]!=0x43415631U || words[2]>CAVE_HALL_SIZE ||
       words[3]!=1 || words[34]!=cave_scores_hash(words,34))return 0;
    memset(out,0,sizeof(*out));out->count=(int)words[2];out->generation=words[1];
    for(int i=0;i<out->count;i++) {
        CaveScore s={words[4+i*3],words[5+i*3],words[6+i*3]};
        if(s.points>CAVE_SCORE_MAX || s.seconds>s.points || s.kills>CAVE_SCORE_MAX ||
           (i && s.points>out->rows[i-1].points))return 0;
        out->rows[i]=s;
    }
    return 1;
}
static inline void cave_scores_load(CaveHall *hall,const char *a,const char *b) {
    CaveHall first,second;int va=cave_scores_read(a,&first),vb=cave_scores_read(b,&second);
    memset(hall,0,sizeof(*hall));hall->slot=-1;
    if(va){*hall=first;hall->slot=0;}
    if(vb && (!va || (int32_t)(second.generation-first.generation)>0)){*hall=second;hall->slot=1;}
}
static inline int cave_scores_save(CaveHall *hall,const char *a,const char *b) {
    uint32_t words[35]={0};int slot=hall->slot==0?1:0;
    words[0]=0x43415631U;words[1]=hall->generation+1;words[2]=hall->count;words[3]=1;
    for(int i=0;i<hall->count;i++) {
        words[4+i*3]=hall->rows[i].points;words[5+i*3]=hall->rows[i].seconds;words[6+i*3]=hall->rows[i].kills;
    }
    words[34]=cave_scores_hash(words,34);
    FILE *f=fopen(slot?b:a,"wb");if(!f)return 0;
    int valid=fwrite(words,sizeof(words),1,f)==1;
    if(fflush(f)!=0)valid=0;
    if(fclose(f)!=0)valid=0;
    if(valid){hall->slot=slot;hall->generation=words[1];}
    return valid;
}
#endif

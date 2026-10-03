/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PSPSTREAMER_CAVE_GAME_H
#define PSPSTREAMER_CAVE_GAME_H
#include <stdint.h>
#include <string.h>
enum { CAVE_GAME_OFF, CAVE_GAME_INTRO, CAVE_GAME_ALIVE, CAVE_GAME_EXPLODING, CAVE_GAME_HALL };
enum { CAVE_SCORE_MAX=9999999, CAVE_HALL_SIZE=10 };
typedef struct { uint32_t points,seconds,kills; } CaveScore;
typedef struct {
    int phase,health,paused,recorded,selection,completed;
    double exit_hold;
    double seconds,death_seconds,protection;
    uint32_t bonus,kills;
} CaveGame;
typedef struct { uint64_t since;int down,latched; } CaveExitHold;
/* Entry press cannot also exit. A release is needed to arm the five-second
 * hold; the clock is real elapsed time, independent of music pause/FPS. */
static inline int cave_game_shoulders(CaveExitHold *h,int down,int active,uint64_t now,double *progress) {
    *progress=0;
    if(!down){memset(h,0,sizeof(*h));return 0;}
    if(!h->down){h->down=1;h->since=now;if(!active){h->latched=1;return 1;}}
    if(h->latched)return 0;
    if(now<h->since)h->since=now;
    *progress=(now-h->since)*.000001;
    if(*progress>=5){h->latched=1;*progress=0;return -1;}
    return 0;
}
static inline void cave_game_start(CaveGame *g) {
    memset(g,0,sizeof(*g));g->phase=CAVE_GAME_ALIVE;g->health=100;
    g->protection=.85; /* Materialization is not a wall hit. */
}
static inline CaveScore cave_game_score(const CaveGame *g) {
    uint32_t seconds=g->seconds<CAVE_SCORE_MAX?(uint32_t)g->seconds:CAVE_SCORE_MAX;
    uint32_t points=g->bonus>CAVE_SCORE_MAX-seconds?CAVE_SCORE_MAX:seconds+g->bonus;
    return (CaveScore){points,seconds,g->kills};
}
static inline void cave_game_tick(CaveGame *g,double seconds) {
    if(g->paused || !(seconds>0))return;
    if(g->phase==CAVE_GAME_ALIVE) {
        g->seconds+=seconds;
        if(g->seconds>CAVE_SCORE_MAX)g->seconds=CAVE_SCORE_MAX;
        g->protection-=seconds;if(g->protection<0)g->protection=0;
    } else if(g->phase==CAVE_GAME_EXPLODING) {
        g->death_seconds+=seconds;
        if(g->death_seconds>=5){g->death_seconds=5;g->phase=CAVE_GAME_HALL;}
    }
}
static inline int cave_game_wall_hit(CaveGame *g) {
    if(g->phase!=CAVE_GAME_ALIVE || g->paused || g->protection>0)return 0;
    g->health-=20;g->protection=1.0;
    if(g->health<=0){g->health=0;g->phase=CAVE_GAME_EXPLODING;g->death_seconds=0;g->completed=1;}
    return 1;
}
static inline int cave_game_blaster_hit(CaveGame *g,int rolling) {
    if(g->phase!=CAVE_GAME_ALIVE || g->paused || rolling || g->protection>0)return 0;
    g->health-=10;
    if(g->health<=0){g->health=0;g->phase=CAVE_GAME_EXPLODING;g->death_seconds=0;g->completed=1;}
    return 1;
}
static inline int cave_game_ram_hit(CaveGame *g) {
    if(g->phase!=CAVE_GAME_ALIVE || g->paused || g->protection>0)return 0;
    g->health-=30;g->protection=1.0;
    if(g->health<=0){g->health=0;g->phase=CAVE_GAME_EXPLODING;g->death_seconds=0;g->completed=1;}
    return 1;
}
/* Future enemy code calls this ONCE on destruction, never for each projectile.
 * 100 is a normal enemy; stronger types supply their explicit reward. */
static inline void cave_game_enemy_destroyed(CaveGame *g,unsigned reward) {
    if(g->phase!=CAVE_GAME_ALIVE || g->paused)return;
    if(reward<100)reward=100;
    g->bonus=reward>CAVE_SCORE_MAX-g->bonus?CAVE_SCORE_MAX:g->bonus+reward;
    if(g->kills<CAVE_SCORE_MAX)g->kills++;
}
/* Stable ties: an equal score never displaces an older entry. */
static inline int cave_hall_insert(CaveScore hall[CAVE_HALL_SIZE],int *count,CaveScore score) {
    int at=0;while(at<*count && hall[at].points>=score.points)at++;
    if(at==CAVE_HALL_SIZE)return -1;
    if(*count<CAVE_HALL_SIZE)(*count)++;
    for(int i=*count-1;i>at;i--)hall[i]=hall[i-1];
    hall[at]=score;return at;
}
#endif

#include "cave_scores.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>
int main(int argc,char **argv) {
    assert(argc==3);CaveGame g;cave_game_start(&g);
    assert(!cave_game_wall_hit(&g) && g.health==100);
    cave_game_tick(&g,1);assert(cave_game_wall_hit(&g) && g.health==80);
    for(int i=0;i<120;i++)assert(!cave_game_wall_hit(&g));
    cave_game_tick(&g,.9);assert(!cave_game_wall_hit(&g));
    g.paused=1;cave_game_tick(&g,100);assert(fabs(g.seconds-1.9)<1e-8);
    assert(!cave_game_wall_hit(&g));cave_game_enemy_destroyed(&g,100);assert(!g.kills);
    g.paused=0;cave_game_tick(&g,.2);assert(cave_game_wall_hit(&g) && g.health==60);
    cave_game_enemy_destroyed(&g,100);cave_game_enemy_destroyed(&g,300);
    assert(cave_game_score(&g).points==402 && g.kills==2);
    for(int i=0;i<3;i++){cave_game_tick(&g,1);assert(cave_game_wall_hit(&g));}
    assert(!g.health && g.completed && g.phase==CAVE_GAME_EXPLODING);
    CaveScore final=cave_game_score(&g);cave_game_enemy_destroyed(&g,100);
    cave_game_tick(&g,4.99);assert(g.phase==CAVE_GAME_EXPLODING);
    cave_game_tick(&g,.02);assert(g.phase==CAVE_GAME_HALL);
    assert(cave_game_score(&g).points==final.points);
    /* Survival score uses real time, not simulation dt or frame count. */
    cave_game_start(&g);for(int i=0;i<30;i++)cave_game_tick(&g,1.0/3);
    assert(g.seconds>9.999 && g.seconds<10.001);
    cave_game_tick(&g,5.5);assert(cave_game_score(&g).seconds==15);
    g.bonus=CAVE_SCORE_MAX-1;cave_game_enemy_destroyed(&g,~0U);
    assert(cave_game_score(&g).points==CAVE_SCORE_MAX);
    cave_game_start(&g);g.protection=0;
    assert(!cave_game_blaster_hit(&g,1) && g.health==100);
    g.paused=1;assert(!cave_game_blaster_hit(&g,0));g.paused=0;
    for(int i=0;i<10;i++)assert(cave_game_blaster_hit(&g,0));
    assert(g.health==0 && g.phase==CAVE_GAME_EXPLODING);
    assert(!cave_game_blaster_hit(&g,0));
    CaveExitHold hold={0};double progress;
    assert(cave_game_shoulders(&hold,1,0,100,&progress)==1);
    assert(!cave_game_shoulders(&hold,1,1,9000000,&progress) && progress==0);
    cave_game_shoulders(&hold,0,1,9000001,&progress);
    assert(!cave_game_shoulders(&hold,1,1,10000000,&progress));
    assert(!cave_game_shoulders(&hold,1,1,14999999,&progress) && progress<5);
    assert(cave_game_shoulders(&hold,1,1,15000000,&progress)==-1);
    assert(!cave_game_shoulders(&hold,1,0,22000000,&progress));
    cave_game_shoulders(&hold,0,0,22000001,&progress);
    assert(cave_game_shoulders(&hold,1,0,23000000,&progress)==1);
    CaveHall hall;cave_scores_load(&hall,argv[1],argv[2]);assert(!hall.count);
    for(int i=0;i<12;i++)cave_hall_insert(hall.rows,&hall.count,(CaveScore){i*10U,i,0});
    assert(hall.count==10 && hall.rows[0].points==110 && hall.rows[9].points==20);
    assert(cave_hall_insert(hall.rows,&hall.count,(CaveScore){20,1,0})==-1);
    assert(cave_scores_save(&hall,argv[1],argv[2]));
    CaveHall read;cave_scores_load(&read,argv[1],argv[2]);assert(read.count==10 && read.rows[0].points==110);
    cave_hall_insert(hall.rows,&hall.count,(CaveScore){200,100,1});assert(cave_scores_save(&hall,argv[1],argv[2]));
    cave_scores_load(&read,argv[1],argv[2]);assert(read.rows[0].points==200);
    FILE *f=fopen(argv[2],"wb");assert(f);fputs("interrupted write",f);fclose(f);
    cave_scores_load(&read,argv[1],argv[2]);assert(read.rows[0].points==110);
    assert(!cave_scores_save(&read,"/no/such/directory/a","/no/such/directory/b"));
    puts("Flight: cooldown, real-time score, pause, fatal hit, five-second explosion/exit, rewards and dual-slot recovery OK");
}

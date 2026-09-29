/* Game-only bounded state. Coordinates remain in tunnel loft space, so cached
 * world rebasing cannot teleport projectiles or stationary enemies. */
#ifndef PSPSTREAMER_CAVE_COMBAT_H
#define PSPSTREAMER_CAVE_COMBAT_H
enum { CAVE_SHIPS=7, CAVE_BOLTS=32, CAVE_BLASTER_DAMAGE=10 };
#define CAVE_PLAYER_FIRE_SECONDS .5f
#define CAVE_ENEMY_FIRE_SECONDS .75f
typedef struct { float p[3],v[3],life; int enemy; } CaveBolt;
typedef struct {
    CaveBolt bolts[CAVE_BOLTS];
    float enemy[3],aim[3],spawn,visible,enemy_cooldown,fire_cooldown,flash;
    int health,model,fire;
    unsigned random;
} CaveCombat;
#endif

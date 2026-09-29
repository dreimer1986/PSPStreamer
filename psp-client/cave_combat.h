/* Game-only bounded state. Coordinates remain in tunnel loft space, so cached
 * world rebasing cannot teleport projectiles or stationary enemies. */
#ifndef PSPSTREAMER_CAVE_COMBAT_H
#define PSPSTREAMER_CAVE_COMBAT_H
enum { CAVE_SHIPS=7, CAVE_BOLTS=32, CAVE_ENEMY_MODEL=7, CAVE_ENEMY_HITS=3 };
#define CAVE_ENEMY_SCALE .60f
#define CAVE_PLAYER_FIRE_SECONDS .5f
#define CAVE_ENEMY_FIRE_SECONDS CAVE_PLAYER_FIRE_SECONDS
typedef struct { float p[3],v[3],life; int enemy; } CaveBolt;
typedef struct {
    CaveBolt bolts[CAVE_BOLTS];
    float enemy[3],aim[3],spawn,visible,enemy_cooldown,fire_cooldown,flash;
    int health,model,fire,pending_spawn;
    float spawn_retry;
    unsigned random;
} CaveCombat;
#endif

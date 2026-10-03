/* Game-only bounded state. Coordinates remain in tunnel loft space, so cached
 * world rebasing cannot teleport projectiles or stationary enemies. */
#ifndef PSPSTREAMER_CAVE_COMBAT_H
#define PSPSTREAMER_CAVE_COMBAT_H
enum { CAVE_SHIPS=7, CAVE_BOLTS=32, CAVE_ENEMY_MODEL=7, CAVE_ENEMY_HITS=3 };
#define CAVE_ENEMY_MAX_HEALTH 15
#define CAVE_ENEMY_SHOT_DAMAGE 5
#define CAVE_ENEMY_RAM_DAMAGE 9
#define CAVE_ENEMY_SCALE .60f
#define CAVE_PLAYER_FIRE_SECONDS .5f
#define CAVE_ENEMY_FIRE_SECONDS CAVE_PLAYER_FIRE_SECONDS
typedef struct { float p[3],v[3],life; int enemy; } CaveBolt;
typedef struct {
    CaveBolt bolts[CAVE_BOLTS];
    float enemy[3],aim[3],spawn,visible,enemy_cooldown,fire_cooldown,flash;
    int health,model,fire,pending_spawn;
    float spawn_retry;
    float waypoint[3],waypoint_wait;
    int drone;
    unsigned spawn_sequence;
    float shield[3],shield_timer,shield_retry,shield_age;
    float previous_player[3];
    int shield_active,previous_player_valid;
    float explosion[3],explosion_age;
    int explosion_active;
    int contact;
    unsigned random;
} CaveCombat;
#endif

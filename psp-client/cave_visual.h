/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PSPSTREAMER_CAVE_VISUAL_H
#define PSPSTREAMER_CAVE_VISUAL_H
#include "milkdrop_warp.h"
#include "cave_paths.h"
#include "cave_bend.h"
enum { CAVE_GRID=12,CAVE_AHEAD=16,CAVE_HISTORY=3,CAVE_SLICES=CAVE_AHEAD+CAVE_HISTORY,
       CAVE_TEXTURE=64,CAVE_MAX_VERTICES=CAVE_GRID*CAVE_GRID*15,
       CAVE_EDGE_SLOTS=6*(CAVE_GRID+1)*(CAVE_GRID+1),
       CAVE_HAIR_VERTICES=2*CAVE_EDGE_SLOTS };
_Static_assert(CAVE_PATH_CACHE>=CAVE_SLICES+3,"Path cache must retain camera and both future field profiles");
typedef struct {int fog,multitexture,hair,transparent_hair,beat,sensitivity,amplitude,style,speed,invert_y,noise,flight_sensitivity,flight_inertia;} CaveOptions;
extern CaveOptions cave_options;
typedef struct {float travel,bass,phase,pulse,bank,roll,spin,forward;unsigned long long previous,last_beat;int direction;} CaveMotion;
typedef struct {
    float field[CAVE_GRID+1][CAVE_GRID+1];
    float gradient[CAVE_GRID+1][CAVE_GRID+1][3];
    int z;
} CavePlane;
typedef struct {
    MdVertex vertices[CAVE_MAX_VERTICES];
    MdVertex secondary[CAVE_MAX_VERTICES];
    MdVertex wire[CAVE_MAX_VERTICES];
    MdVertex hair[CAVE_HAIR_VERTICES];
    int index,count,hair_count,texture_a,texture_b;
    float minimum[3],maximum[3];
    int padding[13]; /* 64-byte DMA alignment. */
} CaveSlice __attribute__((aligned(64)));
typedef struct {
    CaveSlice slices[CAVE_SLICES];
    CaveMotion motion;
    int next,ready,built;
    float noise[16*16*16];
    float noise_matrix[3][9],noise_offset[3][3];
    CavePlane planes[2];
    int sampled_planes;
    CavePaths paths;
    unsigned random;
    int style,black,texture_style,normal_effect;
    float normals[CAVE_MAX_VERTICES][3];
    float rgba[CAVE_MAX_VERTICES][4];
    unsigned short edge_id[CAVE_MAX_VERTICES];
    float material_phase[3];
    float background_phase[3],background_rgb[3];
    float random_values[2048];
    unsigned random_cursor;
    int texture_index[2];
    float texture_transition[2];
    int texture_changed[2];
    float material[CAVE_PATHS][4]; /* Original per-contributor RGBA fields. */
    CavePose poses[CAVE_PATH_CACHE];
    CaveBendController bend;
    int pose_next;
    /* Easter egg: zero-initialized and never consulted by normal motion. */
    int flight,flight_throttle,flight_roll_input,flight_initialized;
    float flight_x,flight_y,flight_axis_x,flight_axis_y;
    float flight_yaw,flight_pitch,flight_roll,flight_speed;
    float flight_roll_velocity;
    float flight_age,flight_impact,flight_stuck,flight_ghost,flight_ghost_age;
    float flight_barrel_time;
    int flight_barrel,flight_last_roll,flight_tap_roll;
    unsigned long long flight_tap_time;
} CaveScene;
/* Model-space extents, scaled exactly like drawing, plus a small wall skin.
 * The narrow collision box rotates with the visible ship. */
#define CAVE_SHIP_SCALE .45f
#define CAVE_SHIP_SKIN .006f
#define CAVE_SHIP_HALF_X (.45f*CAVE_SHIP_SCALE+CAVE_SHIP_SKIN)
#define CAVE_SHIP_HALF_Y (.1109721f*CAVE_SHIP_SCALE+CAVE_SHIP_SKIN)
#define CAVE_SHIP_HALF_Z (.3529831f*CAVE_SHIP_SCALE+CAVE_SHIP_SKIN)
void cave_ship_pose(const CaveScene *s,float center[3],float right[3],float up[3],float forward[3]);
int cave_ship_contact(const CaveScene *s,float x,float y,float z,float normal[3]);
void cave_flight_input(CaveScene *scene,int toggle,int analog_x,int analog_y,int throttle,int roll);
float cave_ship_opacity(const CaveScene *scene);
void cave_world_point(const CaveScene *scene,float x,float y,float z,float out[3]);
void cave_material_sample(const CaveScene *scene,int profile,float x,float y,float fraction,float rgba[4],float normal[3]);
/* Original beat choice and damping, independent of the PSP audio detector. */
int cave_beat_response(CaveMotion *motion,int amplitude,unsigned choice,float dt,int beat);
void cave_texture(uint32_t *pixels);
void cave_noise_rotation(float out[9],float a,float b);
CaveScene *cave_create(void);
CaveScene *cave_create_seed(unsigned seed);
void cave_destroy(CaveScene *scene);
void cave_camera(const CaveScene *scene,float z,float *x,float *y);
/* Column-major OpenGL/GE view: six-profile look-ahead, bounded roll/sway. */
void cave_view(const CaveScene *scene,float z,float matrix[16]);
/* Builds no more than one new slab per tick. Returned slab needs DMA writeback. */
CaveSlice *cave_prepare(CaveScene *scene,const unsigned char bands[12],int level,unsigned long long now);
float cave_density(const CaveScene *scene,float x,float y,float z);
float cave_sample(const CaveScene *scene,float x,float y,float z,float gradient[3]);
/* Independent cube polygonizer, exposed for exhaustive topology bounds checks. */
int cave_polygonize(const float values[8],MdVertex *out,int capacity,float x,float y,float z);
#endif

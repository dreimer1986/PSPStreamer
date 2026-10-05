/* SPDX-License-Identifier: GPL-2.0-or-later
 * The subset of the PSP drawing interface actually used by the shared effects.
 * This is an NV2A adapter, not a PSP emulator. No PSP SDK object is linked.
 */
#ifndef XBOX_VISUAL_GPU_H
#define XBOX_VISUAL_GPU_H
#include <stdint.h>
#include <stddef.h>
typedef struct {float x,y,z,w;} ScePspFVector4;
typedef struct {ScePspFVector4 x,y,z,w;} ScePspFMatrix4;
enum {GU_TEXTURE_2D,GU_BLEND,GU_DEPTH_TEST,GU_FOG,GU_CULL_FACE,GU_LIGHTING,GU_ALPHA_TEST,GU_STENCIL_TEST,GU_SCISSOR_TEST,GU_CLIP_PLANES};
enum {GU_POINTS=1,GU_LINES,GU_LINE_STRIP,GU_TRIANGLES,GU_TRIANGLE_STRIP,GU_TRIANGLE_FAN,GU_SPRITES};
enum {GU_TEXTURE_32BITF=1,GU_COLOR_8888=2,GU_VERTEX_32BITF=4,GU_TRANSFORM_2D=8,GU_TRANSFORM_3D=0};
enum {GU_PSM_8888,GU_PSM_5650,GU_PSM_4444};
enum {GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,GU_FIX,GU_OTHER_COLOR,GU_ONE_MINUS_OTHER_COLOR};
enum {GU_PROJECTION,GU_VIEW,GU_MODEL};
enum {GU_CLAMP,GU_REPEAT,GU_NEAREST,GU_LINEAR};
enum {GU_DIRECT=0,GU_SYNC_FINISH=0,GU_SYNC_WHAT_DONE=0,GU_SMOOTH=0,GU_GEQUAL=0,GU_TFX_MODULATE=0,GU_TCC_RGB=0,GU_TCC_RGBA=1};
enum {GU_COLOR_BUFFER_BIT=1,GU_DEPTH_BUFFER_BIT=2};
extern unsigned char *xv_ram;
extern int xv_width,xv_height;
/* Main-thread-only cooperative audio service; never invokes drawing/input. */
extern void (*xv_service_hook)(void);
void xv_service(void);
int sceGuInit(void);
void sceGuTerm(void);
int sceGuStart(int,void *);
void sceGuFinish(void);
void sceGuSync(int,int);
void *sceGuGetMemory(size_t);
void sceGuDrawArray(int,int,int,const void *,const void *);
void sceGuDrawBufferList(int,void *,int);
void sceGuDepthBuffer(void *,int);
void sceGuDepthMask(int);
void sceGuDepthFunc(int);
void sceGuDepthRange(int,int);
void sceGuEnable(int);
void sceGuDisable(int);
void sceGuBlendFunc(int,int,int,unsigned,unsigned);
void sceGuSetMatrix(int,const ScePspFMatrix4 *);
void sceGuFog(float,float,unsigned);
void sceGuOffset(int,int);
void sceGuViewport(int,int,int,int);
void sceGuScissor(int,int,int,int);
void sceGuShadeModel(int);
void sceGuTexMode(int,int,int,int);
void sceGuTexImage(int,int,int,int,const void *);
void sceGuTexFunc(int,int);
void sceGuTexFilter(int,int);
void sceGuTexWrap(int,int);
void sceGuTexScale(float,float);
void sceGuTexOffset(float,float);
void sceGuTexFlush(void);
void sceGuTexSync(void);
void sceGuSendCommandi(int,int);
void sceGuClear(int);
void sceGuCopyImage(int,int,int,int,int,int,const void *,int,int,int,void *);
void sceKernelDcacheWritebackRange(const void *,size_t);
unsigned long long sceKernelGetSystemTimeWide(void);
static inline unsigned sceGeEdramGetSize(void){return 4*1024*1024;}
int xv_failed(void);
const char *xv_error(void);
void xv_native_texture(void *);
void xv_present_begin(void);
const void *xv_pixels(void);
#endif

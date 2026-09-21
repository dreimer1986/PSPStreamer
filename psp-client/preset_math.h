/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PSPSTREAMER_PRESET_MATH_H
#define PSPSTREAMER_PRESET_MATH_H
enum { PM_MAX_OPS = 4096, PM_TOTAL_OPS = 16384, PM_MAX_RECORDS = 1024, PM_STACK = 48, PM_DEPTH = 64,
       PM_Q_BASE = 55, PM_Q_COUNT = 32, PM_USER_BASE = PM_Q_BASE + PM_Q_COUNT,
       PM_USER_COUNT = 128, PM_COORD_BASE = PM_USER_BASE + PM_USER_COUNT,
       PM_META_BASE = PM_COORD_BASE + 4, PM_DYNAMIC_BASE = PM_META_BASE + 2,
       PM_SHAPE_BASE = PM_DYNAMIC_BASE + 10, PM_T_BASE = PM_SHAPE_BASE + 23,
       PM_WAVE_BASE = PM_T_BASE + 8, PM_EFFECT_BASE = PM_WAVE_BASE + 4,
       PM_ENGINE_BASE = PM_EFFECT_BASE + 5, PM_INPUT_BASE = PM_ENGINE_BASE + 3,
       PM_MONITOR = PM_INPUT_BASE + 6, PM_WRAP = PM_MONITOR + 1, PM_VALUES = PM_WRAP + 1,
       PM_PIXEL_OPS = 4096, PM_MEMORY = 4096, PM_GLOBAL_MEMORY = 8192,
       PM_GLOBAL_ADDRESS_SPACE = 1048576, PM_GLOBAL_PAGE_SIZE = 256,
       PM_FUEL = 4096, PM_PHASE_FUEL = 65536, PM_FRAME_FUEL = 131072 };
typedef struct { float memory[PM_MEMORY]; unsigned int random; } PmRuntime;
typedef struct { int count; char names[PM_USER_COUNT][32]; } PmSymbols;
typedef struct { int op, arg, line; float value; } PmOp;
/* Owns import-time allocation. Zero-initialize; do not shallow-copy owners.
 * Playback never allocates. Empty contexts reserve no bytecode storage. */
typedef struct { int count, lines, capacity; PmOp *code; } PmProgram;
void pm_program_free(PmProgram *program);
void pm_program_compact(PmProgram *program);
typedef struct { int offset, line; } PmSourceLocation;
enum { PM_OK, PM_INVALID, PM_UNSUPPORTED, PM_NOMEM };
/* Compile appends transactionally; source is never retained. */
int pm_compile(PmProgram *program, const char *source, int line);
/* Init/frame share one bounded namespace. Roll back names too on failure. */
int pm_compile_symbols(PmProgram *program, const char *source, int line, PmSymbols *symbols);
int pm_compile_pixel(PmProgram *program, const char *source, int line);
int pm_compile_pixel_symbols(PmProgram *program, const char *source, int line, PmSymbols *symbols);
int pm_compile_shape(PmProgram *program, const char *source, int line, PmSymbols *symbols);
int pm_compile_wave(PmProgram *program, const char *source, int line, PmSymbols *symbols, int point);
/* Compile a complete numbered-record block. Contexts: frame=0, pixel=1,
 * shape=2, wave frame/init=3, wave point=4. Mapping lives only during import. */
int pm_compile_mapped(PmProgram *program,const char *source,const PmSourceLocation *locations,
                      int count,PmSymbols *symbols,int context,int *error_line);
/* values: zoom,rot,warp,warp speed,warp scale,decay,r,g,b,time,
 * psp_low,psp_mid,psp_high,psp_level,psp_low_smooth,psp_mid_smooth,psp_high_smooth.
 * Then bass,mid,treb,bass_att,mid_att,treb_att (context-local mutable inputs).
 * Indices 23..29: translation, center, stretch, zoom exponent.
 * Indices 30..54: MdDecor scalar prefix (wave/echo/border/gamma/alpha outputs).
 * Execution commits values only on success. Lazy branches and structured loops
 * share bounded runtime fuel. Memory writes are journaled. No runtime heap or
 * recursion. See docs/MILKDROP_COMPATIBILITY.md for scopes and limits. */
int pm_execute(const PmProgram *program, float values[PM_VALUES], int *error_line);
int pm_execute_runtime(const PmProgram *program, float values[PM_VALUES], int *error_line, PmRuntime *runtime);
/* Once-per-frame/setup work shares the SAME global frame budget; point VMs
 * retain their smaller invocation cap. Setup recognizes bounded clear loops. */
int pm_execute_frame_runtime(const PmProgram *program,float values[PM_VALUES],int *error_line,PmRuntime *runtime);
int pm_execute_init_runtime(const PmProgram *program,float values[PM_VALUES],int *error_line,PmRuntime *runtime);
/* Call once per visualization frame. Limits total work across all points. */
void pm_begin_frame(void);
/* Conservative scheduling hints; neither function grants additional fuel. */
int pm_frame_remaining(void);
int pm_program_work_hint(const PmProgram *program);
void pm_reset_globals(void);
int pm_assignment_line(const PmProgram *program, int variable);
#endif

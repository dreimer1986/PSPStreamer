/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PSPSTREAMER_PRESET_MATH_H
#define PSPSTREAMER_PRESET_MATH_H
enum { PM_MAX_OPS = 128, PM_STACK = 24, PM_DEPTH = 16,
       PM_Q_BASE = 55, PM_Q_COUNT = 32, PM_USER_BASE = PM_Q_BASE + PM_Q_COUNT,
       PM_USER_COUNT = 16, PM_COORD_BASE = PM_USER_BASE + PM_USER_COUNT,
       PM_VALUES = PM_COORD_BASE + 4, PM_PIXEL_OPS = 64 };
typedef struct { int count; char names[PM_USER_COUNT][32]; } PmSymbols;
typedef struct { int op, arg, line; float value; } PmOp;
typedef struct { int count, lines; PmOp code[PM_MAX_OPS]; } PmProgram;
enum { PM_OK, PM_INVALID, PM_UNSUPPORTED };
/* Compile appends transactionally; source is never retained. */
int pm_compile(PmProgram *program, const char *source, int line);
/* Init/frame share one bounded namespace. Roll back names too on failure. */
int pm_compile_symbols(PmProgram *program, const char *source, int line, PmSymbols *symbols);
int pm_compile_pixel(PmProgram *program, const char *source, int line);
/* values: zoom,rot,warp,warp speed,warp scale,decay,r,g,b,time,
 * psp_low,psp_mid,psp_high,psp_level,psp_low_smooth,psp_mid_smooth,psp_high_smooth.
 * Then bass,mid,treb,bass_att,mid_att,treb_att (read-only relative inputs).
 * Indices 23..29: translation, center, stretch, zoom exponent.
 * Indices 30..54: MdDecor scalar prefix (wave/echo/border/gamma/alpha outputs).
 * Execution commits all values only on success. if() uses bounded forward-only
 * branches; both paths consume compile budget, only the selected path executes.
 * No heap/recursion at runtime. */
int pm_execute(const PmProgram *program, float values[PM_VALUES], int *error_line);
int pm_assignment_line(const PmProgram *program, int variable);
#endif

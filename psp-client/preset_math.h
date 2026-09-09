/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PSPSTREAMER_PRESET_MATH_H
#define PSPSTREAMER_PRESET_MATH_H
enum { PM_MAX_OPS = 128, PM_STACK = 24, PM_DEPTH = 16, PM_VALUES = 55 };
typedef struct { int op, arg, line; float value; } PmOp;
typedef struct { int count, lines; PmOp code[PM_MAX_OPS]; } PmProgram;
enum { PM_OK, PM_INVALID, PM_UNSUPPORTED };
/* Compile appends transactionally; source is never retained. */
int pm_compile(PmProgram *program, const char *source, int line);
/* values: zoom,rot,warp,warp speed,warp scale,decay,r,g,b,time,
 * psp_low,psp_mid,psp_high,psp_level,psp_low_smooth,psp_mid_smooth,psp_high_smooth.
 * Then bass,mid,treb,bass_att,mid_att,treb_att (read-only relative inputs).
 * Indices 23..29: translation, center, stretch, zoom exponent.
 * Indices 30..54: MdDecor scalar prefix (wave/echo/border/gamma/alpha outputs).
 * Execution commits all values only on success. No heap/recursion at runtime. */
int pm_execute(const PmProgram *program, float values[PM_VALUES], int *error_line);
int pm_assignment_line(const PmProgram *program, int variable);
#endif

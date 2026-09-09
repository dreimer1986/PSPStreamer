/* SPDX-License-Identifier: GPL-2.0-or-later
 * New bounded arithmetic subset, not the original NS-EEL engine. */
#include "preset_math.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
enum { PUSH, LOAD, STORE, ADD, SUB, MUL, DIV, NEG, SIN, COS, ABS, MIN, MAX, SQRT,
       FLOOR, CEIL, ATAN, EXP, LOG, LOG10, SQR, SIGN, POW, ATAN2, ABOVE, BELOW, EQUAL };
static int binary(int op) {
    return (op>=ADD && op<=DIV) || op==MIN || op==MAX || (op>=POW && op<=EQUAL);
}
static int function(const char *name) {
    static const struct {const char *name; int op;} list[]={
        {"sin",SIN},{"cos",COS},{"abs",ABS},{"min",MIN},{"max",MAX},{"sqrt",SQRT},
        {"floor",FLOOR},{"ceil",CEIL},{"atan",ATAN},{"exp",EXP},{"log",LOG},
        {"log10",LOG10},{"sqr",SQR},{"sign",SIGN},{"pow",POW},{"atan2",ATAN2},
        {"above",ABOVE},{"below",BELOW},{"equal",EQUAL}};
    for(unsigned int i=0;i<sizeof(list)/sizeof(list[0]);i++)
        if(!strcmp(name,list[i].name)) return list[i].op;
    return -1;
}
int pm_assignment_line(const PmProgram *program, int variable) {
    for (int i = program->count-1; i >= 0; i--)
        if (program->code[i].op == STORE && program->code[i].arg == variable)
            return program->code[i].line;
    return 0;
}
typedef struct { const char *p; PmProgram *code; int line, depth, error; } Parser;
static void space(Parser *p) { while (isspace((unsigned char)*p->p)) p->p++; }
static int emit(Parser *p, int op, int arg, float value) {
    if (p->code->count >= PM_MAX_OPS) { p->error = PM_INVALID; return 0; }
    p->code->code[p->code->count++] = (PmOp){op,arg,p->line,value};
    return 1;
}
static int name(Parser *p, char text[32]) {
    int n = 0;
    space(p);
    if (!isalpha((unsigned char)*p->p) && *p->p != '_') return 0;
    while (isalnum((unsigned char)*p->p) || *p->p == '_') {
        if (n == 31) { p->error = PM_INVALID; return 0; }
        text[n++] = *p->p++;
    }
    text[n] = 0; return 1;
}
static int variable(const char *s) {
    static const char *names[PM_VALUES] = {"zoom","rot","warp","","","decay",
        "wave_r","wave_g","wave_b","time","psp_low","psp_mid","psp_high",
        "psp_level","psp_low_smooth","psp_mid_smooth","psp_high_smooth",
        "bass","mid","treb","bass_att","mid_att","treb_att","dx","dy",
        "cx","cy","sx","sy","zoomexp",
        "wave_x","wave_y","wave_mystery","wave_dots","wave_thick","wave_additive","wave_brighten",
        "wave_mod_alpha","wave_mod_start","wave_mod_end","echo_zoom","echo_alpha","echo_orient",
        "ob_size","ob_r","ob_g","ob_b","ob_a","ib_size","ib_r","ib_g","ib_b","ib_a",
        "gamma","wave_a"};
    for (int i = 0; i < PM_VALUES; i++) if (*names[i] && !strcmp(s,names[i])) return i;
    return -1;
}
static int expression(Parser *p);
static int unary(Parser *p) {
    char text[32], *end;
    int ok = 0;
    space(p);
    if (++p->depth > PM_DEPTH) { p->error = PM_INVALID; return 0; }
    if (*p->p == '+' || *p->p == '-') {
        int negative = *p->p++ == '-';
        ok = unary(p) && (!negative || emit(p,NEG,0,0));
    } else if (*p->p == '(') {
        p->p++; ok = expression(p); space(p);
        if (*p->p != ')') ok = 0; else p->p++;
    } else if (isdigit((unsigned char)*p->p) || *p->p == '.') {
        float value;
        errno = 0; value = strtof(p->p,&end);
        if (end != p->p && errno != ERANGE && isfinite(value)) {
            p->p = end; ok = emit(p,PUSH,0,value);
        }
    } else if (name(p,text)) {
        space(p);
        if (*p->p == '(') {
            int op = function(text);
            if (op < 0) p->error = PM_UNSUPPORTED;
            else {
                p->p++; ok = expression(p); space(p);
                if (ok && binary(op)) {
                    if (*p->p != ',') ok = 0;
                    else { p->p++; ok = expression(p); space(p); }
                }
                if (*p->p != ')') ok = 0;
                else { p->p++; ok = ok && emit(p,op,0,0); }
            }
        } else {
            int id = variable(text);
            if (id < 0) p->error = PM_UNSUPPORTED;
            else ok = emit(p,LOAD,id,0);
        }
    }
    p->depth--;
    return ok;
}
static int term(Parser *p) {
    if (!unary(p)) return 0;
    for (;;) {
        int op; space(p);
        if (*p->p != '*' && *p->p != '/') return 1;
        op = *p->p++ == '*' ? MUL : DIV;
        if (!unary(p) || !emit(p,op,0,0)) return 0;
    }
}
static int expression(Parser *p) {
    if (!term(p)) return 0;
    for (;;) {
        int op; space(p);
        if (*p->p != '+' && *p->p != '-') return 1;
        op = *p->p++ == '+' ? ADD : SUB;
        if (!term(p) || !emit(p,op,0,0)) return 0;
    }
}
int pm_compile(PmProgram *program, const char *source, int line) {
    int before = program->count;
    Parser p = {source,program,line,0,PM_INVALID};
    space(&p);
    if (!*p.p || program->lines >= 16 || before < 0 || before > PM_MAX_OPS) return PM_INVALID;
    while (*p.p) {
        char text[32]; int id;
        if (!name(&p,text)) goto fail;
        id = variable(text);
        if (id < 0 || (id >= 9 && id < 23)) { p.error = PM_UNSUPPORTED; goto fail; }
        space(&p);
        if (*p.p++ != '=') goto fail;
        if (!expression(&p) || !emit(&p,STORE,id,0)) goto fail;
        space(&p);
        if (*p.p != ';') goto fail;
        p.p++; space(&p);
    }
    program->lines++;
    return PM_OK;
fail:
    program->count = before;
    return p.error;
}
int pm_execute(const PmProgram *program, float values[PM_VALUES], int *error_line) {
    float local[PM_VALUES], stack[PM_STACK];
    int used = 0;
    memcpy(local,values,sizeof(local)); *error_line = 0;
    if (program->count < 0 || program->count > PM_MAX_OPS) return 0;
    for (int i = 0; i < program->count; i++) {
        const PmOp *op = &program->code[i];
        float a, b = 0, result = 0;
        *error_line = op->line;
        if (op->op == PUSH || op->op == LOAD) {
            if (used >= PM_STACK) return 0;
            if (op->op == LOAD && (op->arg < 0 || op->arg >= PM_VALUES)) return 0;
            result = op->op == PUSH ? op->value : local[op->arg];
            if (!isfinite(result)) return 0;
            stack[used++] = result; continue;
        }
        if (op->op == STORE) {
            if (used != 1 || op->arg < 0 || op->arg >= PM_VALUES ||
                (op->arg >= 9 && op->arg < 23)) return 0;
            local[op->arg] = stack[--used]; continue;
        }
        if (!used) return 0;
        a = stack[--used];
        if (binary(op->op)) {
            if (!used) return 0;
            b = a; a = stack[--used];
        }
        switch (op->op) {
            case ADD: result=a+b; break; case SUB: result=a-b; break;
            case MUL: result=a*b; break;
            case DIV: if (b==0) return 0; result=a/b; break;
            case NEG: result=-a; break; case SIN: result=sinf(a); break;
            case COS: result=cosf(a); break; case ABS: result=fabsf(a); break;
            case MIN: result=fminf(a,b); break; case MAX: result=fmaxf(a,b); break;
            case SQRT: if (a<0) return 0; result=sqrtf(a); break;
            case FLOOR: result=floorf(a); break; case CEIL: result=ceilf(a); break;
            case ATAN: result=atanf(a); break; case EXP: result=expf(a); break;
            case LOG: if(a<=0) return 0; result=logf(a); break;
            case LOG10: if(a<=0) return 0; result=log10f(a); break;
            case SQR: result=a*a; break; case SIGN: result=(a>0)-(a<0); break;
            case POW: result=powf(a,b); break; case ATAN2: result=atan2f(a,b); break;
            case ABOVE: result=a>b; break; case BELOW: result=a<b; break;
            case EQUAL: result=fabsf(a-b)<.00001f; break;
            default: return 0;
        }
        if (!isfinite(result)) return 0;
        stack[used++] = result;
    }
    if (used) return 0;
    memcpy(values,local,sizeof(local)); *error_line=0; return 1;
}

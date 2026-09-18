/* SPDX-License-Identifier: GPL-2.0-or-later
 * New bounded arithmetic subset, not the original NS-EEL engine. */
#include "preset_math.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <stdint.h>
enum { PUSH, LOAD, STORE, ADD, SUB, MUL, DIV, NEG, SIN, COS, ABS, MIN, MAX, SQRT,
       FLOOR, CEIL, ATAN, EXP, LOG, LOG10, SQR, SIGN, POW, ATAN2, ABOVE, BELOW, EQUAL,
       TAN, ASIN, ACOS, BNOT, BAND, BOR, SIGMOID, IF, JZ, JUMP,
       DROP, KEEP, MOD, BITAND, BITOR, NEQ, LE, GE, BOOL, RAND, INVSQRT,
       REGL, REGS, MEML, MEMS, GMEML, GMEMS, DUP, LOOP, LOOPEND, WHILE, WHILEEND,
       EXEC2, EXEC3, MEMCPY, MEMSET, FREEMBUF, ASSIGN };
static int binary(int op) {
    return (op>=ADD && op<=DIV) || op==MIN || op==MAX || (op>=POW && op<=EQUAL) ||
           op==BAND || op==BOR || op==SIGMOID || (op>=MOD && op<=GE);
}
static int function(const char *name) {
    static const struct {const char *name; int op;} list[]={
        {"sin",SIN},{"cos",COS},{"abs",ABS},{"min",MIN},{"max",MAX},{"sqrt",SQRT},
        {"floor",FLOOR},{"ceil",CEIL},{"atan",ATAN},{"exp",EXP},{"log",LOG},
        {"log10",LOG10},{"sqr",SQR},{"sign",SIGN},{"pow",POW},{"atan2",ATAN2},
        {"above",ABOVE},{"below",BELOW},{"equal",EQUAL},
        {"tan",TAN},{"asin",ASIN},{"acos",ACOS},{"bnot",BNOT},
        {"band",BAND},{"bor",BOR},{"sigmoid",SIGMOID},{"if",IF},
        {"rand",RAND},{"invsqrt",INVSQRT},{"loop",LOOP},{"while",WHILE},
        {"exec2",EXEC2},{"exec3",EXEC3},{"megabuf",MEML},{"gmegabuf",GMEML},
        {"memcpy",MEMCPY},{"memset",MEMSET},{"freembuf",FREEMBUF},
        {"assign",ASSIGN},{"int",FLOOR}};
    for(unsigned int i=0;i<sizeof(list)/sizeof(list[0]);i++)
        if(!strcmp(name,list[i].name)) return list[i].op;
    return -1;
}
int pm_assignment_line(const PmProgram *program, int variable) {
    for (int i = program->count-1; i >= 0; i--)
        if ((program->code[i].op == STORE || program->code[i].op == KEEP) && program->code[i].arg == variable)
            return program->code[i].line;
    return 0;
}
typedef struct { const char *p; PmProgram *code; int line, depth, error; PmSymbols *symbols; int pixel; } Parser;
static void space(Parser *p) {
    for (;;) {
        while (isspace((unsigned char)*p->p)) p->p++;
        if (p->p[0]=='/' && p->p[1]=='/') {p->p+=strlen(p->p);return;}
        if (p->p[0]!='/' || p->p[1]!='*') return;
        const char *end=strstr(p->p+2,"*/");
        if (!end) {p->error=PM_INVALID;return;}
        p->p=end+2;
    }
}
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
        text[n++] = (char)tolower((unsigned char)*p->p++);
    }
    text[n] = 0; return 1;
}
static int builtin_variable(const char *s) {
    if(!strcmp(s,"wave_usedots")) return 33;
    if(!strcmp(s,"wrap")) return PM_WRAP;
    if(!strcmp(s,"monitor")) return PM_MONITOR;
    static const char *inputs[]={"meshx","meshy","pixelsx","pixelsy","aspectx","aspecty"};
    for(int i=0;i<6;i++) if(!strcmp(s,inputs[i])) return PM_INPUT_BASE+i;
    if(!strcmp(s,"progress")) return PM_ENGINE_BASE+2;
    static const char *effects[]={"darken_center","brighten","darken","solarize","invert"};
    for(int i=0;i<5;i++) if(!strcmp(s,effects[i])) return PM_EFFECT_BASE+i;
    if(!strcmp(s,"frame")) return PM_META_BASE;
    if(!strcmp(s,"fps")) return PM_META_BASE+1;
    static const char *dynamic[]={"wave_mode","mv_a","mv_r","mv_g","mv_b","mv_x","mv_y","mv_dx","mv_dy","mv_l"};
    for(int i=0;i<10;i++) if(!strcmp(s,dynamic[i])) return PM_DYNAMIC_BASE+i;
    static const char *names[PM_Q_BASE] = {"zoom","rot","warp","","","decay",
        "wave_r","wave_g","wave_b","time","psp_low","psp_mid","psp_high",
        "psp_level","psp_low_smooth","psp_mid_smooth","psp_high_smooth",
        "bass","mid","treb","bass_att","mid_att","treb_att","dx","dy",
        "cx","cy","sx","sy","zoomexp",
        "wave_x","wave_y","wave_mystery","wave_dots","wave_thick","wave_additive","wave_brighten",
        "wave_mod_alpha","wave_mod_start","wave_mod_end","echo_zoom","echo_alpha","echo_orient",
        "ob_size","ob_r","ob_g","ob_b","ob_a","ib_size","ib_r","ib_g","ib_b","ib_a",
        "gamma","wave_a"};
    for (int i = 0; i < PM_Q_BASE; i++) if (*names[i] && !strcmp(s,names[i])) return i;
    if (s[0]=='q' && s[1]>='1' && s[1]<='9') {
        int number=0;
        for (int i=1;s[i];i++) {
            if (s[i]<'0' || s[i]>'9') return -1;
            number=number*10+s[i]-'0';
            if (number>PM_Q_COUNT) return -1;
        }
        return PM_Q_BASE+number-1;
    }
    return -1;
}
static int variable(Parser *p,const char *s) {
    if(p->pixel>=3) {
        static const char *names[]={"r","g","b","a"};
        for(int i=0;i<4;i++) if(!strcmp(s,names[i])) return PM_SHAPE_BASE+10+i;
        if(!strcmp(s,"samples")) return PM_WAVE_BASE;
        if(p->pixel==4) {
            if(!strcmp(s,"x")) return PM_SHAPE_BASE+4;
            if(!strcmp(s,"y")) return PM_SHAPE_BASE+5;
            if(!strcmp(s,"sample")) return PM_WAVE_BASE+1;
            if(!strcmp(s,"value1")) return PM_WAVE_BASE+2;
            if(!strcmp(s,"value2")) return PM_WAVE_BASE+3;
        }
        if(s[0]=='t' && s[1]>='1' && s[1]<='8' && !s[2]) return PM_T_BASE+s[1]-'1';
    }
    if(p->pixel==2) {
        if(!strcmp(s,"instance")) return PM_ENGINE_BASE;
        if(!strcmp(s,"instances") || !strcmp(s,"num_inst")) return PM_ENGINE_BASE+1;
        static const char *names[]={"enabled","sides","additive","textured","x","y","rad","ang","tex_ang","tex_zoom","r","g","b","a","r2","g2","b2","a2","border_r","border_g","border_b","border_a","thick"};
        for(int i=0;i<23;i++) if(!strcmp(s,names[i])) return PM_SHAPE_BASE+i;
        if(s[0]=='t' && s[1]>='1' && s[1]<='8' && !s[2]) return PM_T_BASE+s[1]-'1';
    }
    if(p->pixel==1) {
        static const char *coords[]={"x","y","rad","ang"};
        for(int i=0;i<4;i++) if(!strcmp(s,coords[i])) return PM_COORD_BASE+i;
    }
    int id=builtin_variable(s);
    if(id>=0) {
        if(p->pixel>=2 && !((id>=9 && id<23) || (id>=PM_Q_BASE && id<PM_USER_BASE) || (id>=PM_META_BASE && id<PM_DYNAMIC_BASE) || id==PM_ENGINE_BASE+2 || (id>=PM_INPUT_BASE && id<PM_MONITOR))) goto unsupported;
        if(p->pixel==1 && !(id<3 || id==5 || (id>=9 && id<=29) ||
                        (id>=PM_Q_BASE && id<PM_USER_BASE) || id>=PM_META_BASE)) goto unsupported;
        return id;
    }
    /* Do not silently turn unsupported engine inputs or misspelled q/t/reg
     * registers into user state. Built-in function names are reserved too. */
    static const char *reserved[]={"nan","inf","infinity","fps","frame","progress",
        "monitor","x","y","rad","ang","sample","samples","value1","value2",
        "meshx","meshy","pixelsx","pixelsy","aspectx","aspecty"};
    if(!p->symbols || function(s)>=0) goto unsupported;
    for(unsigned int i=0;i<sizeof(reserved)/sizeof(reserved[0]);i++)
        if(!strcmp(s,reserved[i])) goto unsupported;
    /* t registers belong to custom wave/shape contexts. In global/pixel
     * code (e.g. Geiss Artifact's t2), these are ordinary named variables. */
    if(((s[0]=='q' || s[0]=='Q' || (s[0]=='t' && p->pixel>=2)) && isdigit((unsigned char)s[1])) ||
       (!strncmp(s,"reg",3) && isdigit((unsigned char)s[3]))) goto unsupported;
    for(int i=0;i<p->symbols->count;i++)
        if(!strcmp(s,p->symbols->names[i])) return PM_USER_BASE+i;
    if(p->symbols->count>=PM_USER_COUNT) { p->error=PM_INVALID; return -1; }
    id=p->symbols->count++;
    strcpy(p->symbols->names[id],s); /* tokenizer bounds names to 31 bytes */
    return PM_USER_BASE+id;
unsupported:
    p->error=PM_UNSUPPORTED;
    return -1;
}
#include "preset_expression.h"
int pm_compile(PmProgram *program, const char *source, int line) {
    return compile_context(program,source,line,NULL,0);
}
int pm_compile_symbols(PmProgram *program, const char *source, int line, PmSymbols *symbols) {
    return compile_context(program,source,line,symbols,0);
}
int pm_compile_pixel(PmProgram *program, const char *source, int line) {
    return compile_context(program,source,line,NULL,1);
}
int pm_compile_pixel_symbols(PmProgram *program,const char *source,int line,PmSymbols *symbols) {
    return compile_context(program,source,line,symbols,1);
}
int pm_compile_shape(PmProgram *program, const char *source, int line, PmSymbols *symbols) {
    return compile_context(program,source,line,symbols,2);
}
int pm_compile_wave(PmProgram *program, const char *source, int line, PmSymbols *symbols, int point) {
    return compile_context(program,source,line,symbols,point?4:3);
}
static float global_memory[PM_MEMORY], registers[100];
static PmRuntime fallback_runtime;
static int frame_fuel=-1;
void pm_begin_frame(void) {frame_fuel=262144;}
void pm_reset_globals(void) {
    memset(global_memory,0,sizeof(global_memory));memset(registers,0,sizeof(registers));
    memset(&fallback_runtime,0,sizeof(fallback_runtime));frame_fuel=-1;
}
/* Only memory-using programs pay for journaling. Roll back on invalid math,
 * bytecode, address, or exhausted execution budget; no allocations in playback. */
typedef struct {float *address,old;} Write;
enum {JOURNAL_SIZE=2*PM_MEMORY+100};
typedef struct {int count;PmRuntime *runtime;unsigned int dirty[(JOURNAL_SIZE+31)/32];Write writes[JOURNAL_SIZE];} Journal;
static int write_value(Journal *j,float *address,float value) {
    /* Most point formulas never write memory/registers. Initialize their
     * rollback bitmap only if such a write actually occurs. */
    if(!j->count) memset(j->dirty,0,sizeof(j->dirty));
    uintptr_t a=(uintptr_t)address;
    int id;
    if(a>=(uintptr_t)j->runtime->memory && a<(uintptr_t)(j->runtime->memory+PM_MEMORY)) id=(int)((a-(uintptr_t)j->runtime->memory)/sizeof(float));
    else if(a>=(uintptr_t)global_memory && a<(uintptr_t)(global_memory+PM_MEMORY)) id=PM_MEMORY+(int)((a-(uintptr_t)global_memory)/sizeof(float));
    else if(a>=(uintptr_t)registers && a<(uintptr_t)(registers+100)) id=2*PM_MEMORY+(int)((a-(uintptr_t)registers)/sizeof(float));
    else return 0;
    unsigned int bit=1U<<(id&31);
    if(!(j->dirty[id/32]&bit)) {
        if(j->count>=JOURNAL_SIZE) return 0;
        j->dirty[id/32]|=bit;j->writes[j->count++]=(Write){address,*address};
    }
    *address=value;return 1;
}
static int address_index(float x) {
    if(!isfinite(x) || x<0 || x>=PM_MEMORY) return -1;
    int i=(int)(x+.00001f);return i<PM_MEMORY?i:-1;
}
static int execute(const PmProgram *program,float values[PM_VALUES],int *error_line,PmRuntime *runtime,Journal *journal) {
    float local[PM_VALUES], stack[PM_STACK];
    struct {int start,end,left,stack;} loops[PM_DEPTH];
    int used = 0,depth=0,fuel=PM_FUEL;
    memcpy(local,values,sizeof(local)); *error_line = 0;
    if (program->count < 0 || program->count > PM_MAX_OPS) return 0;
    for (int i = 0; i < program->count; i++) {
        const PmOp *op = &program->code[i];
        if(--fuel<0 || frame_fuel==0) return 0;
        if(frame_fuel>0) frame_fuel--;
        float a, b = 0, result = 0;
        *error_line = op->line;
        if (op->op == PUSH || op->op == LOAD) {
            if (used >= PM_STACK) return 0;
            if (op->op == LOAD && (op->arg < 0 || op->arg >= PM_VALUES)) return 0;
            result = op->op == PUSH ? op->value : local[op->arg];
            if (!isfinite(result)) return 0;
            stack[used++] = result; continue;
        }
        if (op->op == STORE || op->op==KEEP) {
            if (!used || (op->op==STORE && used!=1) || op->arg < 0 || (op->arg>=PM_ENGINE_BASE && op->arg!=PM_MONITOR && op->arg!=PM_WRAP) || (op->arg>PM_WAVE_BASE && op->arg<PM_EFFECT_BASE) ||
                (op->arg >= 9 && op->arg < 23) ||
                (op->arg>=PM_COORD_BASE && op->arg<PM_DYNAMIC_BASE)) return 0;
            local[op->arg] = stack[used-1];if(op->op==STORE) used--;continue;
        }

        if(op->op==LOOP || op->op==WHILE) {
            if(depth>=PM_DEPTH || op->arg<=i+1 || op->arg>program->count ||
               program->code[op->arg-1].op!=(op->op==LOOP?LOOPEND:WHILEEND) || program->code[op->arg-1].arg!=i) return 0;
            int count=PM_FUEL;
            if(op->op==LOOP) {
                if(!used) return 0;
                a=stack[--used];count=a<1?0:a>PM_FUEL?PM_FUEL:(int)a;
                if(!count) {stack[used++]=0;i=op->arg-1;continue;}
            }
            loops[depth].start=i;loops[depth].end=op->arg-1;
            loops[depth].left=count;loops[depth++].stack=used;continue;
        }
        if(op->op==LOOPEND || op->op==WHILEEND) {
            if(!depth || loops[depth-1].start!=op->arg || loops[depth-1].end!=i ||
               used!=loops[depth-1].stack+1) return 0;
            int again=op->op==LOOPEND?--loops[depth-1].left>0:fabsf(stack[used-1])>=.00001f;
            if(again) {used--;i=op->arg;} else depth--;
            continue;
        }
        if(op->op==DROP) {if(!used) return 0;used--;continue;}
        if(op->op==DUP) {if(!used || used>=PM_STACK) return 0;stack[used]=stack[used-1];used++;continue;}
        if(op->op==REGL || op->op==REGS) {
            if(op->arg<0 || op->arg>=100) return 0;
            if(op->op==REGL) {if(used>=PM_STACK) return 0;stack[used++]=registers[op->arg];}
            else if(!used || !write_value(journal,&registers[op->arg],stack[used-1])) return 0;
            continue;
        }
        if(op->op==MEML || op->op==GMEML || op->op==MEMS || op->op==GMEMS) {
            int store=op->op==MEMS || op->op==GMEMS;
            if(used<1+store) return 0;
            int index=address_index(stack[used-1-store]);if(index<0) return 0;
            float *memory=(op->op==GMEML || op->op==GMEMS)?global_memory:runtime->memory;
            if(store) {
                a=stack[--used];if(!write_value(journal,memory+index,a)) return 0;
                stack[used-1]=a;
            } else stack[used-1]=memory[index];
            continue;
        }
        if(op->op==MEMCPY || op->op==MEMSET) {
            if(used<3) return 0;
            float count=stack[used-1];int dst=address_index(stack[used-3]);
            if(count<0 || count>PM_MEMORY || dst<0) return 0;
            int n=(int)count,src=op->op==MEMCPY?address_index(stack[used-2]):0;
            if(dst+n>PM_MEMORY || src<0 || (op->op==MEMCPY && src+n>PM_MEMORY)) return 0;
            if(n>fuel) return 0;
            fuel-=n;
            if(frame_fuel>=0) {
                if(n>frame_fuel) {frame_fuel=0;return 0;}
                frame_fuel-=n;
            }
            a=stack[used-2];
            for(int k=0;k<n;k++) {
                int j=op->op==MEMCPY && dst>src?n-1-k:k;
                if(!write_value(journal,runtime->memory+dst+j,op->op==MEMCPY?runtime->memory[src+j]:a)) return 0;
            }
            used-=2;continue;
        }
        if (op->op==JZ || op->op==JUMP) {
            /* Corrupt bytecode must not introduce loops or escape the program. */
            if (op->arg<=i || op->arg>program->count) return 0;
            if (op->op==JUMP) { i=op->arg-1; continue; }
            if (!used) return 0;
            a=stack[--used];
            if (fabsf(a)<.00001f) i=op->arg-1;
            continue;
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
            case NEQ: result=fabsf(a-b)>=.00001f;break;
            case LE: result=a<=b;break;case GE:result=a>=b;break;
            case MOD: {
                double aa=floor(fabs((double)a)),bb=floor(fabs((double)b));
                if(aa>4294967295.0 || bb>4294967295.0) return 0;
                result=bb==0?0:(float)((uint32_t)aa%(uint32_t)bb);break;
            }
            case BITAND: case BITOR:
                if((double)a < -9223372036854775808.0 || (double)a >= 9223372036854775808.0 ||
                   (double)b < -9223372036854775808.0 || (double)b >= 9223372036854775808.0) return 0;
                result=(float)(op->op==BITAND?((int64_t)a & (int64_t)b):((int64_t)a | (int64_t)b));break;
            case BOOL: result=fabsf(a)>=.00001f;break;
            case RAND: {
                unsigned int r=runtime->random?runtime->random:0x9e3779b9U;
                r^=r<<13;r^=r>>17;r^=r<<5;runtime->random=r;
                result=(float)((double)r/4294967295.0)*fmaxf(1,floorf(a));break;
            }
            case INVSQRT: if(a<=0) return 0;result=1/sqrtf(a);break;
            /* Fixed memory is already reserved. EEL freembuf is only a hint;
             * it must not erase data observable by subsequent expressions. */
            case FREEMBUF: result=a;break;
            case TAN: result=tanf(a); break;
            case ASIN: if(fabsf(a)>1) return 0; result=asinf(a); break;
            case ACOS: if(fabsf(a)>1) return 0; result=acosf(a); break;
            case BNOT: result=fabsf(a)<.00001f; break;
            /* EEL's named band/bor functions evaluate both arguments, and use
             * > epsilon (unlike if/bnot's < epsilon false test). */
            case BAND: result=fabsf(a)>.00001f && fabsf(b)>.00001f; break;
            case BOR: result=fabsf(a)>.00001f || fabsf(b)>.00001f; break;
            case SIGMOID: {
                /* Algebraically 1/(1+exp(-a*b)); avoid exponential overflow.
                 * Overflow of the finite-input product correctly saturates. */
                float z=a*b;
                if(z>=0) result=1/(1+expf(-z));
                else { float e=expf(z); result=e/(1+e); }
                break;
            }
            default: return 0;
        }
        if (!isfinite(result)) return 0;
        stack[used++] = result;
    }
    if (used || depth) return 0;
    memcpy(values,local,sizeof(local)); *error_line=0; return 1;
}
int pm_execute_runtime(const PmProgram *program,float values[PM_VALUES],int *error_line,PmRuntime *runtime) {
    if(!program->count) {*error_line=0;return 1;}
    Journal journal; journal.count=0;journal.runtime=runtime;
    unsigned int random=runtime->random;
    if(execute(program,values,error_line,runtime,&journal)) return 1;
    while(journal.count) {Write *w=&journal.writes[--journal.count];*w->address=w->old;}
    runtime->random=random;return 0;
}
int pm_execute(const PmProgram *program,float values[PM_VALUES],int *error_line) {
    return pm_execute_runtime(program,values,error_line,&fallback_runtime);
}

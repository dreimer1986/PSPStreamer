/* SPDX-License-Identifier: GPL-2.0-or-later
 * New bounded arithmetic subset, not the original NS-EEL engine. */
#include "preset_math.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include "preset_trig.h"
#ifdef __PSP__
#include <pspkernel.h>
#endif
#ifdef PM_AUDIT
/* Host collection audits only; no diagnostic writes/strings in PSP builds. */
static const char *audit_reason="";
const char *pm_audit_reason(void) {return audit_reason;}
void pm_audit_reset(void) {audit_reason="";}
#define PM_REASON(reason) (audit_reason=(reason))
#else
#define PM_REASON(reason) ((void)0)
#endif
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
void pm_runtime_copy(PmRuntime *destination,const PmRuntime *source) {
    if(destination==source)return;
    memcpy(destination->memory,source->memory,(size_t)source->page_count*PM_GLOBAL_PAGE_SIZE*sizeof(float));
    memcpy(&destination->random,&source->random,sizeof(*source)-offsetof(PmRuntime,random));
}
void pm_program_free(PmProgram *program) {
    free(program->code);memset(program,0,sizeof(*program));
}
void pm_program_compact(PmProgram *program) {
    if(!program->count){int lines=program->lines;pm_program_free(program);program->lines=lines;return;}
    if(program->capacity==program->count)return;
    PmOp *code=realloc(program->code,(size_t)program->count*sizeof(*code));
    if(code){program->code=code;program->capacity=program->count;}
}
static int program_valid(const PmProgram *p) {
    return p->count>=0 && p->count<=p->capacity && p->capacity<=PM_MAX_OPS &&
        p->capacity>=0 && (!p->capacity || p->code);
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
int pm_program_work_hint(const PmProgram *program) {
    if(!program_valid(program))return PM_FUEL;
    for(int i=0;i<program->count;i++) {
        int op=program->code[i].op;
        if(op==LOOP || op==WHILE || op==MEMCPY || op==MEMSET)return PM_FUEL;
    }
    return program->count;
}
int pm_assignment_line(const PmProgram *program, int variable) {
    if(!program_valid(program))return 0;
    for (int i = program->count-1; i >= 0; i--)
        if ((program->code[i].op == STORE || program->code[i].op == KEEP) && program->code[i].arg == variable)
            return program->code[i].line;
    return 0;
}
typedef struct { const char *p; PmProgram *code; int line, depth, error; PmSymbols *symbols; int pixel;
    const char *source; const PmSourceLocation *locations; int location_count; } Parser;
static int source_line(const Parser *p,int previous) {
    if(!p->location_count)return p->line;
    const char *at=p->p;
    if(previous && at>p->source) {
        at--;
        while(at>p->source && isspace((unsigned char)*at))at--;
    }
    int lo=0,hi=p->location_count;
    while(lo+1<hi) {
        int mid=(lo+hi)/2;
        if(p->locations[mid].offset<=at-p->source)lo=mid;else hi=mid;
    }
    return p->locations[lo].line;
}
static void space(Parser *p) {
    for (;;) {
        while (isspace((unsigned char)*p->p)) p->p++;
        if (p->p[0]=='/' && p->p[1]=='/') {
            while(*p->p && *p->p!='\n')p->p++;
            continue;
        }
        if (p->p[0]!='/' || p->p[1]!='*') return;
        const char *end=strstr(p->p+2,"*/");
        if (!end) {p->error=PM_INVALID;return;}
        p->p=end+2;
    }
}
static int emit(Parser *p, int op, int arg, float value) {
    if (p->code->count >= PM_MAX_OPS) { PM_REASON("bytecode capacity");p->error = PM_INVALID; return 0; }
    if(p->code->count==p->code->capacity) {
        int capacity=p->code->capacity?p->code->capacity*2:64;
        if(capacity>PM_MAX_OPS)capacity=PM_MAX_OPS;
        PmOp *code=realloc(p->code->code,(size_t)capacity*sizeof(*code));
        if(!code){p->error=PM_NOMEM;return 0;}
        p->code->code=code;p->code->capacity=capacity;
    }
    p->code->code[p->code->count++] = (PmOp){op,arg,source_line(p,1),value};
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
        if(p->pixel==3 && !strcmp(s,"samples")) return PM_WAVE_BASE;
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
        if(p->pixel>=2 && !((id>=9 && id<23) || (id>=PM_Q_BASE && id<PM_USER_BASE) || (id>=PM_META_BASE && id<PM_DYNAMIC_BASE) || id==PM_ENGINE_BASE+2 || (id>=PM_INPUT_BASE && id<PM_MONITOR))) goto local;
        if(p->pixel==1 && !(id<3 || id==5 || (id>=9 && id<=29) ||
                        (id>=PM_Q_BASE && id<PM_USER_BASE) || id>=PM_META_BASE)) goto local;
        return id;
    }
local:
    /* Desktop VMs register fields per context; an identifier absent there
     * becomes ordinary VM-local state, even if another context knows it.
     * Only actual q1..32/t1..8/reg00..99 are special, not their prefixes.
     * Keep non-finite spellings and function names reserved in this parser. */
    ;
    static const char *reserved[]={"nan","inf","infinity"};
    if(!p->symbols || function(s)>=0) goto unsupported;
    for(unsigned int i=0;i<sizeof(reserved)/sizeof(reserved[0]);i++)
        if(!strcmp(s,reserved[i])) goto unsupported;
    for(int i=0;i<p->symbols->count;i++)
        if(!strcmp(s,p->symbols->names[i])) return PM_USER_BASE+i;
    if(p->symbols->count>=PM_USER_COUNT) { PM_REASON("variable capacity");p->error=PM_INVALID; return -1; }
    id=p->symbols->count++;
    strcpy(p->symbols->names[id],s); /* tokenizer bounds names to 31 bytes */
    return PM_USER_BASE+id;
unsupported:
    p->error=PM_UNSUPPORTED;
    return -1;
}
#include "preset_expression.h"
int pm_compile_mapped(PmProgram *program,const char *source,const PmSourceLocation *locations,
                      int count,PmSymbols *symbols,int context,int *error_line) {
    if(count<1 || count>PM_MAX_RECORDS || context<0 || context>4)return PM_INVALID;
    return compile_context_mapped(program,source,locations[0].line,symbols,context,locations,count,error_line);
}
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
static float global_memory[PM_GLOBAL_MEMORY], registers[100];
static unsigned int resource_limits;
void (*pm_diagnostic_hook)(const char *event,int line,int detail);
unsigned int pm_resource_limits(void) {return resource_limits;}
/* Desktop gmegabuf addresses need not be dense. Map the bounded 64-KiB data
 * pool into 256-value pages anywhere in its 1-Mi-value address space.
 * No heap allocation, eviction or aliasing. At capacity, new-page stores
 * are discarded and recorded by resource_limits. Reads
 * and zero stores to untouched pages require no resident storage. */
enum { GLOBAL_PAGES=PM_GLOBAL_MEMORY/PM_GLOBAL_PAGE_SIZE };
static unsigned char global_page_map[PM_GLOBAL_ADDRESS_SPACE/PM_GLOBAL_PAGE_SIZE];
static unsigned short global_page_keys[GLOBAL_PAGES];
static int global_page_count;
_Static_assert(GLOBAL_PAGES<256,"global page encoding");
static float *global_cell(int index,int create) {
    int page=index/PM_GLOBAL_PAGE_SIZE,physical=global_page_map[page];
    if(!physical && create) {
        if(global_page_count>=GLOBAL_PAGES){resource_limits|=PM_LIMIT_GLOBAL_PAGES;return NULL;}
        physical=++global_page_count;
        global_page_keys[physical-1]=(unsigned short)page;
        global_page_map[page]=(unsigned char)physical;
        /* Free pages are already zero: reset clears the pool and a failed
         * call journals every write before releasing its new reservations.
         * Do not clear twice or move init work into the frame-call budget. */
    }
    return physical?global_memory+(physical-1)*PM_GLOBAL_PAGE_SIZE+index%PM_GLOBAL_PAGE_SIZE:NULL;
}
static PmRuntime fallback_runtime;
static int frame_fuel=-1;
void pm_begin_frame(void) {frame_fuel=PM_TOTAL_FUEL;}
void pm_reset_globals(void) {
    resource_limits=0;
    pm_trig_reset();
    memset(global_memory,0,sizeof(global_memory));memset(registers,0,sizeof(registers));
    memset(global_page_map,0,sizeof(global_page_map));global_page_count=0;
    memset(&fallback_runtime,0,sizeof(fallback_runtime));frame_fuel=-1;
}
int pm_frame_remaining(void) {return frame_fuel<0?PM_TOTAL_FUEL:frame_fuel;}
/* Only memory-using programs pay for journaling. Roll back on invalid math,
 * bytecode, address, or exhausted execution budget; no allocations in playback. */
typedef struct {float *address,old;} Write;
enum {JOURNAL_SIZE=PM_MEMORY+PM_GLOBAL_MEMORY+100};
typedef struct {int count;PmRuntime *runtime;unsigned int dirty[(JOURNAL_SIZE+31)/32];Write writes[JOURNAL_SIZE];} Journal;
static int write_value(Journal *j,float *address,float value) {
    /* Most point formulas never write memory/registers. Initialize their
     * rollback bitmap only if such a write actually occurs. */
    if(!j->count) memset(j->dirty,0,sizeof(j->dirty));
    uintptr_t a=(uintptr_t)address;
    int id;
    if(a>=(uintptr_t)j->runtime->memory && a<(uintptr_t)(j->runtime->memory+PM_MEMORY)) id=(int)((a-(uintptr_t)j->runtime->memory)/sizeof(float));
    else if(a>=(uintptr_t)global_memory && a<(uintptr_t)(global_memory+PM_GLOBAL_MEMORY)) id=PM_MEMORY+(int)((a-(uintptr_t)global_memory)/sizeof(float));
    else if(a>=(uintptr_t)registers && a<(uintptr_t)(registers+100)) id=PM_MEMORY+PM_GLOBAL_MEMORY+(int)((a-(uintptr_t)registers)/sizeof(float));
    else return 0;
    unsigned int bit=1U<<(id&31);
    if(!(j->dirty[id/32]&bit)) {
        if(j->count>=JOURNAL_SIZE) return 0;
        j->dirty[id/32]|=bit;j->writes[j->count++]=(Write){address,*address};
    }
    *address=value;return 1;
}
static int bounded_address(float x,int limit) {
    if(!isfinite(x) || x<0 || x>=limit) return -1;
    int i=(int)(x+.00001f);return i<limit?i:-1;
}
static int address_index(float x) {return bounded_address(x,PM_GLOBAL_ADDRESS_SPACE);}
static int global_index(float x) {
    if(!isfinite(x))return -1;
    /* NSEEL's shared GRAM masks its signed integer address into 1 Mi cells. */
    double value=(double)x+.00001;
    int32_t index=value>=2147483648.0 || value< -2147483648.0?INT32_MIN:(int32_t)value;
    return (int)((uint32_t)index&(PM_GLOBAL_ADDRESS_SPACE-1));
}
#include "preset_local_memory.h"
/* NSEEL's default double assignment clears exceptional/denormal values.
 * Use the corresponding float exponent here (the PSP VM remains float).
 * The expression still returns its RHS; compound stores bypass this filter. */
static float assigned_value(float value) {
    uint32_t bits;memcpy(&bits,&value,sizeof(bits));
    bits&=0x7f800000U;
    return !bits || bits==0x7f800000U?0:value;
}
/* x87's C0 comparison flag is also set for unordered (NaN) operands. */
static int eel_below(float a,float b) {return !(a>=b);}
/* Desktop x87 FISTP (round toward zero) returns integer-indefinite for
 * NaN/Inf/out-of-range inputs. Reproduce the bit pattern without undefined
 * C float-to-integer conversions or changing the PSP's FPU control state. */
static int64_t eel_bit_integer(float value) {
    if(!isfinite(value) || (double)value < -9223372036854775808.0 ||
       (double)value >= 9223372036854775808.0)return INT64_MIN;
    return (int64_t)value;
}
static uint32_t eel_mod_integer(float value) {
    value=fabsf(value);
    if(!isfinite(value) || (double)value>=2147483648.0)return 0x80000000U;
    return (uint32_t)value;
}
/* Variable writes are sparse in point programs. Save the original value only
 * on first assignment; successful execution needs no whole-array copies. */
typedef struct {
    unsigned int dirty[(PM_VALUES+31)/32];
    float old[PM_VALUES];
    unsigned short ids[PM_VALUES];
    int count;
} ValueJournal;
static void variable_store(ValueJournal *variables,float *local,int id,float value) {
    unsigned int bit=1U<<(id&31);
    if(!(variables->dirty[id/32]&bit)) {
        variables->dirty[id/32]|=bit;variables->old[id]=local[id];
        variables->ids[variables->count++]=(unsigned short)id;
    }
    local[id]=value;
}
#include "preset_clear_loop.h"
static int execute(const PmProgram *program,float local[PM_VALUES],int *error_line,PmRuntime *runtime,Journal *journal,ValueJournal *variables,int allowance,int init) {
    float stack[PM_STACK];
    struct {int start,end,left,stack;} loops[PM_DEPTH];
    int used = 0,depth=0,fuel=allowance;
    *error_line = 0;
    if (!program_valid(program)) return 0;
    for (int i = 0; i < program->count; i++) {
        const PmOp *op = &program->code[i];
#ifdef __PSP__
        /* Larger finite setup/frame work must still yield to DAC/network
         * workers. Point programs retain their original small hard cap. */
        if(allowance>PM_FUEL && fuel<allowance && !(fuel&4095)) {
            if(pm_diagnostic_hook)pm_diagnostic_hook("VM yield begin",op->line,allowance-fuel);
            sceKernelDelayThread(1);
            if(pm_diagnostic_hook)pm_diagnostic_hook("VM yield end",op->line,allowance-fuel);
        }
#endif
        if(--fuel<0) {PM_REASON("invocation fuel");return 0;}
        if(frame_fuel==0) {PM_REASON("aggregate frame fuel");return 0;}
        if(frame_fuel>0) frame_fuel--;
        float a, b = 0, result = 0;
        *error_line = op->line;
        if (op->op == PUSH || op->op == LOAD) {
            if (used >= PM_STACK) return 0;
            if (op->op == LOAD && (op->arg < 0 || op->arg >= PM_VALUES)) return 0;
            result = op->op == PUSH ? op->value : local[op->arg];
            if (op->op==PUSH && !isfinite(result)) return 0;
            stack[used++] = result; continue;
        }
        if (op->op == STORE || op->op==KEEP) {
            if (!used || (op->op==STORE && used!=1) || op->arg < 0 || op->arg>=PM_VALUES ||
                (op->arg >= 10 && op->arg < 17)) return 0;
            variable_store(variables,local,op->arg,op->value?stack[used-1]:assigned_value(stack[used-1]));
            if(op->op==STORE) used--;
            continue;
        }

        /* Dense point formulas spend most instructions here. Do not walk
         * all loop/register/memory/branch cases for elementary arithmetic.
         * One original instruction, one fuel charge, same float operation;
         * no fusion, fast-math, skipped assignments or changed error lines. */
        if(op->op>=ADD && op->op<=MUL) {
            if(used<2)return 0;
            b=stack[--used];a=stack[used-1];
            stack[used-1]=op->op==ADD?a+b:op->op==SUB?a-b:a*b;
            continue;
        }

        /* The original scalar opcodes form a contiguous range. Keep their
         * hot path out of the extended control/memory dispatch entirely. */
        if(op->op>=IF) {
        if(op->op==LOOP || op->op==WHILE) {
            if(depth>=PM_DEPTH || op->arg<=i+1 || op->arg>program->count ||
               program->code[op->arg-1].op!=(op->op==LOOP?LOOPEND:WHILEEND) || program->code[op->arg-1].arg!=i) return 0;
            int count=PM_FUEL;
            if(op->op==LOOP) {
                if(!used) return 0;
                a=stack[--used];if(!isfinite(a))return 0;
                if(init) {
                    float value=0;
                    int cleared=clear_loop(program,i,a,local,runtime,journal,variables,&fuel,&value);
                    if(cleared<0)return 0;
                    if(cleared){stack[used++]=value;i=op->arg-1;continue;}
                }
                count=a<1?0:a>1048576?1048576:(int)a;
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
            else if(!used || !write_value(journal,&registers[op->arg],op->value?stack[used-1]:assigned_value(stack[used-1]))) return 0;
            continue;
        }
        if(op->op==MEML || op->op==GMEML || op->op==MEMS || op->op==GMEMS) {
            int store=op->op==MEMS || op->op==GMEMS;
            if(used<1+store) return 0;
            int global=op->op==GMEML || op->op==GMEMS;
            int index=global?global_index(stack[used-1-store]):address_index(stack[used-1-store]);
            if(index<0) {PM_REASON(global?"global address":"local address");return 0;}
            float *cell=global?global_cell(index,0):local_cell(runtime,index,0);
            if(store) {
                a=stack[--used];float value=op->value?a:assigned_value(a);
                if(!global) {
                    if(!local_write(runtime,index,value,journal))return 0;
                    stack[used-1]=a;continue;
                }
                if(!cell && (value!=0 || signbit(value))) {
                    cell=global_cell(index,1);
                }
                if(cell && !write_value(journal,cell,value))return 0;
                stack[used-1]=a;
            } else stack[used-1]=cell?*cell:global?0:local_default(runtime,index);
            continue;
        }
        if(op->op==MEMCPY || op->op==MEMSET) {
            if(used<3) return 0;
            float count=stack[used-1];int dst=address_index(stack[used-3]);
            if(!isfinite(count) || count<0 || count>PM_GLOBAL_ADDRESS_SPACE || dst<0) return 0;
            int n=(int)count,src=op->op==MEMCPY?address_index(stack[used-2]):0;
            if(dst+n>PM_GLOBAL_ADDRESS_SPACE || src<0 || (op->op==MEMCPY && src+n>PM_GLOBAL_ADDRESS_SPACE)) return 0;
            if(n>fuel) return 0;
            fuel-=n;
            if(frame_fuel>=0) {
                if(n>frame_fuel) {frame_fuel=0;return 0;}
                frame_fuel-=n;
            }
            a=stack[used-2];
            for(int k=0;k<n;k++) {
#ifdef __PSP__
                if(k && !(k&4095))sceKernelDelayThread(1);
#endif
                int j=op->op==MEMCPY && dst>src?n-1-k:k;
                if(!local_write(runtime,dst+j,op->op==MEMCPY?local_read(runtime,src+j):a,journal)) return 0;
            }
            used-=2;continue;
        }
        if (op->op==JZ || op->op==JUMP) {
            /* Corrupt bytecode must not introduce loops or escape the program. */
            if (op->arg<=i || op->arg>program->count) return 0;
            if (op->op==JUMP) { i=op->arg-1; continue; }
            if (!used) return 0;
            a=stack[--used];
            if (eel_below(fabsf(a),.00001f)) i=op->arg-1;
            continue;
        }
        }
        if (!used) return 0;
        a = stack[--used];
        if (binary(op->op)) {
            if (!used) return 0;
            b = a; a = stack[--used];
        }
        switch (op->op) {
            case DIV:
                /* Do not issue a hardware divide-by-zero on the PSP. Keep
                 * IEEE intermediates until assignment, as Desktop does. */
                result=b!=0?a/b:(a==0 || isnan(a))?NAN:
                    copysignf(INFINITY,signbit(a)!=signbit(b)?-1.0f:1.0f);break;
            case NEG: result=-a; break; case SIN: result=pm_trig(a,0); break;
            case COS: result=pm_trig(a,1); break; case ABS: result=fabsf(a); break;
            case MIN: result=eel_below(a,b)?a:b; break;
            case MAX: result=eel_below(a,b)?b:a; break;
            /* Desktop NSEEL uses fabs followed by fsqrt, not C sqrt's
             * negative-input domain error (MilkDrop2 asm-nseel-x86-*.c). */
            case SQRT: result=sqrtf(fabsf(a)); break;
            case FLOOR: result=floorf(a); break; case CEIL: result=ceilf(a); break;
            case ATAN: result=atanf(a); break; case EXP: result=expf(a); break;
            case LOG: result=a==0?-INFINITY:a<0?NAN:logf(a); break;
            case LOG10: result=a==0?-INFINITY:a<0?NAN:log10f(a); break;
            case SQR: result=a*a; break; case SIGN: result=a==0?a:copysignf(1,a); break;
            case POW: result=powf(a,b); break; case ATAN2: result=atan2f(a,b); break;
            case ABOVE: result=eel_below(b,a); break; case BELOW: result=eel_below(a,b); break;
            case EQUAL: result=eel_below(fabsf(a-b),.00001f); break;
            case NEQ: result=fabsf(a-b)>=.00001f;break;
            case LE: result=a<=b;break;case GE:result=a>=b;break;
            case MOD: {
                uint32_t aa=eel_mod_integer(a),bb=eel_mod_integer(b);
                result=bb==0?0:(float)(aa%bb);break;
            }
            case BITAND: case BITOR:
                result=(float)(op->op==BITAND?(eel_bit_integer(a)&eel_bit_integer(b)):
                    (eel_bit_integer(a)|eel_bit_integer(b)));break;
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
            case ASIN: result=fabsf(a)>1?NAN:asinf(a); break;
            case ACOS: result=fabsf(a)>1?NAN:acosf(a); break;
            case BNOT: result=eel_below(fabsf(a),.00001f); break;
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
        stack[used++] = result;
    }
    if (used || depth) return 0;
    *error_line=0; return 1;
}
static int execute_runtime(const PmProgram *program,float values[PM_VALUES],int *error_line,PmRuntime *runtime,int allowance,int init) {
    PM_REASON("");
    if(!program->count) {*error_line=0;return 1;}
    /* VM/global registers already belong to the sole visual renderer thread.
     * Keep enlarged bounded rollback scratch off the PSP's 256-KiB stack. */
    static Journal journal; journal.count=0;journal.runtime=runtime;
    static ValueJournal variables;
    variables.count=0;memset(variables.dirty,0,sizeof(variables.dirty));
    unsigned int random=runtime->random;
    int old_pages=global_page_count;
    int old_local_pages=runtime->page_count,old_fills=runtime->fill_count;
    PmFill fills[PM_LOCAL_FILLS];memcpy(fills,runtime->fills,sizeof(fills));
    /* Main-frame/setup phases are bounded separately from point geometry.
     * Their extra allowance must not increase old presets' mesh/wave density
     * or starve all geometry after an expensive one-time initialization. */
    int saved_frame=frame_fuel,separate=allowance>PM_FUEL;
    if(separate)frame_fuel=-1;
    if(separate && pm_diagnostic_hook)pm_diagnostic_hook(init?"VM init begin":"VM frame begin",program->code && program->count>0?program->code[0].line:0,program->count);
    int success=execute(program,values,error_line,runtime,&journal,&variables,allowance,init);
    if(separate && pm_diagnostic_hook)pm_diagnostic_hook(init?"VM init end":"VM frame end",*error_line,success);
    if(separate)frame_fuel=saved_frame;
    if(success) return 1;
    while(variables.count) {
        int id=variables.ids[--variables.count];values[id]=variables.old[id];
    }
    while(journal.count) {Write *w=&journal.writes[--journal.count];*w->address=w->old;}
    while(global_page_count>old_pages)global_page_map[global_page_keys[--global_page_count]]=0;
    while(runtime->page_count>old_local_pages) {
        int page=--runtime->page_count;
        runtime->pages[runtime->keys[page]]=0;runtime->keys[page]=0;
        memset(runtime->memory+page*PM_GLOBAL_PAGE_SIZE,0,PM_GLOBAL_PAGE_SIZE*sizeof(float));
    }
    runtime->fill_count=old_fills;memcpy(runtime->fills,fills,sizeof(fills));
    runtime->random=random;return 0;
}
int pm_execute_runtime(const PmProgram *p,float v[PM_VALUES],int *line,PmRuntime *r) {
    return execute_runtime(p,v,line,r,PM_FUEL,0);
}
int pm_execute_frame_runtime(const PmProgram *p,float v[PM_VALUES],int *line,PmRuntime *r) {
    return execute_runtime(p,v,line,r,PM_FRAME_FUEL,0);
}
int pm_execute_init_runtime(const PmProgram *p,float v[PM_VALUES],int *line,PmRuntime *r) {
    return execute_runtime(p,v,line,r,PM_PHASE_FUEL,1);
}
int pm_execute(const PmProgram *program,float values[PM_VALUES],int *error_line) {
    return pm_execute_runtime(program,values,error_line,&fallback_runtime);
}

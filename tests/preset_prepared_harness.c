/* Differential correctness only: no timing/benchmark workload. */
#include "preset_math.c"
#include <assert.h>
#include <stdio.h>
static unsigned int yields;
int sceKernelDelayThread(unsigned int delay){assert(delay==1);yields++;return 0;}
static uint32_t hash=2166136261U;
static void digest(const void *data,size_t size) {
    const unsigned char *p=data;while(size--)hash=(hash^*p++)*16777619U;
}
static void run(PmProgram *p,int allowance,int aggregate,float seed) {
    static PmRuntime runtime;
    memset(&runtime,0,sizeof(runtime));runtime.random=123;
    frame_fuel=aggregate;yields=0;
    float v[PM_VALUES]={0};v[PM_Q_BASE]=seed;v[PM_Q_BASE+1]=.5f;
    int line=-1,ok=execute_runtime(p,v,&line,&runtime,allowance,0);
    digest(&ok,sizeof(ok));digest(&line,sizeof(line));digest(&frame_fuel,sizeof(frame_fuel));
    digest(&yields,sizeof(yields));digest(v,sizeof(v));digest(&runtime,sizeof(runtime));
    digest(registers,sizeof(registers));
}
int main(void) {
    const char *sources[]={
        "q1=q1+2;q2=q1*3-4;q3=2*q2+q1;",
        "q1=q1+2-3+4*5-6+q2*2-1+q2;q3=q1+1+2+3+4+5+6+7;",
        "q1=16777216+1-16777216+1-0*2+3;q2=(-0.0)*2+0.0-0.0;",
        "q1=16777216+1-16777216;q2=3-(2-1);q3=1e-30*1e-20;",
        "q1=(0/0)+2;q2=(1/0)*0;q3=(-1/0)-2;",
        "q1=1;q2=loop(5,q1=q1*2+1);q3=if(q1,q1+2,q1*3);",
        "reg00=4;megabuf(0)=3;q1=reg00+megabuf(0)*2;q2=q1-7;",
        "q1=42;q2=megabuf(-1)+3;",
        "q1=rand(10)*2+rand(5);q2=q1*q1;",
        "q3=pow(q1,2)+atan2(q1,q2);q4=log(q2)+exp(q2);q5=asin(q1)+acos(q1);",
        "q2=q1?7*q1:3*q1;q3=if(q2,q1+2,q1-3);"
    };
    int prepared=0;
    for(unsigned f=0;f<sizeof(sources)/sizeof(*sources);f++) {
        PmProgram p={0};PmSymbols symbols={0};
        assert(pm_compile_symbols(&p,sources[f],17,&symbols)==PM_OK);
        int count=p.count;pm_program_compact(&p);pm_program_compact(&p);assert(p.count==count);
        for(int i=0;i<p.count;i++)prepared+=p.code[i].op>=FAST_PUSH_ADD;
        for(int allowance=0;allowance<=80;allowance++)for(int budget=-1;budget<=80;budget++) {
            pm_reset_globals();run(&p,allowance,budget,.25f);
        }
        /* Warm cache across invocations, changing inputs and signed zero. */
        pm_reset_globals();
        for(int i=0;i<64;i++)run(&p,PM_FUEL,PM_TOTAL_FUEL,i%4==0?-0.0f:i%4==1?0.0f:i%4==2?.25f:-.25f);
        /* Appending to an already prepared program keeps indices stable. */
        assert(pm_compile_symbols(&p,"q6=q1*7+2;",31,&symbols)==PM_OK);
        pm_program_compact(&p);pm_reset_globals();run(&p,PM_FUEL,PM_TOTAL_FUEL,.5f);
        pm_program_free(&p);
    }
#ifndef PM_REFERENCE_EXECUTION
    assert(prepared>0);
    int slot;float result;
    pm_math_cache_reset();
    assert(!pm_math_cache_lookup(POW,2,3,&result,&slot) && slot>=0);
    pm_math_cache_store(slot,8);assert(pm_math_cache_lookup(POW,2,3,&result,&slot) && result==8);
    assert(!pm_math_cache_lookup(POW,3,2,&result,&slot));
    assert(!pm_math_cache_lookup(ATAN2,0.0f,1,&result,&slot));pm_math_cache_store(slot,0);
    assert(!pm_math_cache_lookup(ATAN2,-0.0f,1,&result,&slot));
    assert(!pm_math_cache_lookup(RAND,2,0,&result,&slot) && slot==-1);
#else
    assert(!prepared);
#endif
    /* The second instruction in a prepared pair must not skip a PSP yield. */
    PmProgram p={0};PmSymbols symbols={0};char source[12000]={0};
    for(int i=0;i<400;i++)strcat(source,"q1=q1*.9+1-2+3*.5+2;");
    assert(pm_compile_symbols(&p,source,42,&symbols)==PM_OK);pm_program_compact(&p);
    pm_reset_globals();run(&p,PM_FRAME_FUEL,PM_TOTAL_FUEL,.25f);assert(yields>0);
    pm_program_free(&p);
    printf("%08x\n",hash);
}

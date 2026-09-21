#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "preset_math.h"
static PmRuntime runtime,baseline;
static float values[PM_VALUES],saved[PM_VALUES];
static PmProgram program;
static PmSymbols symbols;
static void compile(const char *source) {
    pm_program_free(&program);memset(&symbols,0,sizeof(symbols));
    assert(pm_compile_symbols(&program,source,42,&symbols)==PM_OK);
}
int main(void) {
    int line;
    /* The native pattern preserves final loop expression/index and resident
     * memory exactly for small loops accepted by the original scalar VM. */
    for(int count=1;count<140;count+=7) {
        char text[256];snprintf(text,sizeof(text),
            "gmegabuf(12)=23;n=7;q1=loop(%d,megabuf(n)=0;gmegabuf(n)=0;n+=1;);q2=n;q3=gmegabuf(12);",count);
        compile(text);memset(&runtime,0,sizeof(runtime));
        for(int j=0;j<PM_MEMORY;j++)runtime.memory[j]=(float)j;
        baseline=runtime;memset(values,0,sizeof(values));pm_reset_globals();pm_begin_frame();
        assert(pm_execute_runtime(&program,values,&line,&runtime));
        memcpy(saved,values,sizeof(saved));PmRuntime reference=runtime;
        runtime=baseline;memset(values,0,sizeof(values));pm_reset_globals();pm_begin_frame();
        assert(pm_execute_init_runtime(&program,values,&line,&runtime));
        assert(!memcmp(saved,values,sizeof(values)) && !memcmp(&reference,&runtime,sizeof(runtime)));
    }
    /* Large pure clears are bounded by resident PSP memory, not loop count. */
    compile("n=-5000;q1=loop(200000,megabuf(n)=0;gmegabuf(n)=0;n=n+1;);q2=n;");
    memset(values,0,sizeof(values));pm_reset_globals();pm_begin_frame();
    assert(pm_execute_init_runtime(&program,values,&line,&runtime));
    assert(values[PM_Q_BASE]==195000 && values[PM_Q_BASE+1]==195000);
    assert(pm_frame_remaining()>240000);
    for(int i=0;i<PM_MEMORY;i++)assert(runtime.memory[i]==0);
    compile("n=0;q1=loop(2000000000,gmegabuf(n)=0;n+=1;);");pm_begin_frame();
    assert(pm_execute_init_runtime(&program,values,&line,&runtime));
    assert(values[PM_Q_BASE]==1048576);
    /* Expanded global storage does not expand every local PmRuntime. */
    compile("gmegabuf(7000)=17;reg99=21;megabuf(3)=9;");
    assert(pm_execute_runtime(&program,values,&line,&runtime));
    compile("n=0;loop(50000,megabuf(n)=0;gmegabuf(n)=0;n+=1;);reg99=0;megabuf(-1)=0;");
    memcpy(saved,values,sizeof(values));pm_begin_frame();
    assert(!pm_execute_init_runtime(&program,values,&line,&runtime));
    assert(!memcmp(saved,values,sizeof(values)) && runtime.memory[3]==9);
    compile("q1=gmegabuf(7000);q2=reg99;");
    assert(pm_execute_runtime(&program,values,&line,&runtime));
    assert(values[PM_Q_BASE]==17 && values[PM_Q_BASE+1]==21);
    compile("q1=gmegabuf(1048576);");assert(!pm_execute_runtime(&program,values,&line,&runtime));
    compile("q1=megabuf(4096);");assert(!pm_execute_runtime(&program,values,&line,&runtime));
    /* No recognition of loops with another side effect/nonzero fills. */
    compile("n=0;loop(200000,megabuf(n)=1;n+=1;);");pm_begin_frame();
    assert(!pm_execute_init_runtime(&program,values,&line,&runtime));
    compile("n=0;loop(200000,megabuf(n)=0;q1+=1;n+=1;);");pm_begin_frame();
    assert(!pm_execute_init_runtime(&program,values,&line,&runtime));
    /* Per-point fuel remains small; once-per-frame work shares the fixed
     * global pool. A huge loop must fail, never silently stop at 4096. */
    compile("q1=0;loop(1024,q1+=1;);");pm_begin_frame();
    assert(!pm_execute_runtime(&program,values,&line,&runtime));pm_begin_frame();
    assert(pm_execute_frame_runtime(&program,values,&line,&runtime));
    assert(values[PM_Q_BASE]==1024);
    compile("loop(1000000,q1+=1;);");pm_begin_frame();
    assert(!pm_execute_frame_runtime(&program,values,&line,&runtime));
    assert(pm_frame_remaining()==262144-PM_FRAME_FUEL);
    assert(!pm_execute_frame_runtime(&program,values,&line,&runtime));
    assert(pm_frame_remaining()==0);
    compile("q1=1;");assert(!pm_execute_init_runtime(&program,values,&line,&runtime));
    /* Sparse high addresses share across contexts, without aliasing. */
    pm_reset_globals();pm_begin_frame();
    compile("gmegabuf(0)=3;gmegabuf(50000)=9;gmegabuf(1048575)=13;"
            "q1=gmegabuf(50000)+gmegabuf(0);q2=gmegabuf(49999);");
    assert(pm_execute_runtime(&program,values,&line,&runtime));
    assert(values[PM_Q_BASE]==12 && values[PM_Q_BASE+1]==0);
    compile("n=49999;loop(2,gmegabuf(n)=0;n+=1;);q1=gmegabuf(50000);q2=gmegabuf(1048575);");
    assert(pm_execute_init_runtime(&program,values,&line,&runtime));
    assert(values[PM_Q_BASE]==0 && values[PM_Q_BASE+1]==13);
    /* A failed invocation rolls back both values AND new page reservations. */
    pm_reset_globals();pm_begin_frame();
    compile("n=0;loop(32,gmegabuf(n)=2;n+=256;);megabuf(-1)=0;");
    assert(!pm_execute_frame_runtime(&program,values,&line,&runtime));
    compile("n=50000;loop(32,gmegabuf(n)=7;n+=256;);q1=gmegabuf(0);");
    assert(pm_execute_frame_runtime(&program,values,&line,&runtime));
    assert(values[PM_Q_BASE]==0);
    compile("gmegabuf(50000)=99;gmegabuf(0)=1;");
    assert(!pm_execute_runtime(&program,values,&line,&runtime));
    compile("q1=gmegabuf(50000);q2=gmegabuf(0);");
    assert(pm_execute_runtime(&program,values,&line,&runtime));
    assert(values[PM_Q_BASE]==7 && values[PM_Q_BASE+1]==0);
    /* Reading and zeroing untouched addresses never consumes page capacity. */
    pm_reset_globals();pm_begin_frame();
    compile("n=0;loop(4096,q1=gmegabuf(n);gmegabuf(n)=0;n+=256;);");
    assert(pm_execute_frame_runtime(&program,values,&line,&runtime));
    compile("gmegabuf(1048575)=15;q1=gmegabuf(1048575);");
    assert(pm_execute_runtime(&program,values,&line,&runtime));
    assert(values[PM_Q_BASE]==15);
    /* First use must not charge a second page clear against the frame budget.
     * The pool is zero from reset/rollback, so first/repeated work is equal. */
    pm_reset_globals();pm_begin_frame();
    compile("n=0;loop(32,gmegabuf(n)=1;n+=256;);");
    assert(pm_execute_frame_runtime(&program,values,&line,&runtime));
    int first_fuel=pm_frame_remaining();pm_begin_frame();
    assert(pm_execute_frame_runtime(&program,values,&line,&runtime));
    assert(pm_frame_remaining()==first_fuel);
    /* Page-local untouched cells must still be zero when a released physical
     * page is reused at another logical address. */
    pm_reset_globals();pm_begin_frame();
    compile("gmegabuf(50000)=17;gmegabuf(50001)=19;megabuf(-1)=0;");
    assert(!pm_execute_runtime(&program,values,&line,&runtime));
    compile("gmegabuf(0)=2;q1=gmegabuf(80);q2=gmegabuf(81);");
    assert(pm_execute_runtime(&program,values,&line,&runtime));
    assert(values[PM_Q_BASE]==0 && values[PM_Q_BASE+1]==0);
    /* Clear rollback respects logical high-page addresses, not physical slots. */
    compile("gmegabuf(1048575)=15;");
    assert(pm_execute_runtime(&program,values,&line,&runtime));
    compile("n=1048570;loop(6,gmegabuf(n)=0;n+=1;);megabuf(-1)=0;");
    assert(!pm_execute_init_runtime(&program,values,&line,&runtime));
    compile("q1=gmegabuf(1048575);");
    assert(pm_execute_runtime(&program,values,&line,&runtime));
    assert(values[PM_Q_BASE]==15);
    pm_program_free(&program);
    return 0;
}

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "preset_math.h"
static PmRuntime runtime,baseline;
static float values[PM_VALUES],saved[PM_VALUES];
static PmProgram program;
static PmSymbols symbols;
static unsigned int diagnostic_seen;
static void diagnostic(const char *event,int line,int detail) {
    assert(event && line>=0 && detail>=0);
    if(!strcmp(event,"VM init begin"))diagnostic_seen|=1;
    if(!strcmp(event,"VM local fill end"))diagnostic_seen|=2;
    if(!strcmp(event,"VM shared clear end"))diagnostic_seen|=4;
    if(!strcmp(event,"VM init end"))diagnostic_seen|=8;
}
static void compile(const char *source) {
    pm_program_free(&program);memset(&symbols,0,sizeof(symbols));
    assert(pm_compile_symbols(&program,source,42,&symbols)==PM_OK);
}
int main(void) {
    int line;
    pm_diagnostic_hook=diagnostic;
    /* The native pattern preserves final loop expression/index and resident
     * memory exactly for small loops accepted by the original scalar VM. */
    for(int count=1;count<140;count+=7) {
        char text[256];snprintf(text,sizeof(text),
            "gmegabuf(12)=23;n=7;q1=loop(%d,megabuf(n)=0;gmegabuf(n)=0;n+=1;);q2=n;q3=gmegabuf(12);",count);
        compile(text);memset(&runtime,0,sizeof(runtime));
        for(int j=0;j<PM_MEMORY;j++)runtime.memory[j]=(float)j;
        runtime.page_count=PM_MEMORY/PM_GLOBAL_PAGE_SIZE;
        for(int j=0;j<runtime.page_count;j++){runtime.pages[j]=(unsigned char)(j+1);runtime.keys[j]=(unsigned short)j;}
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
    compile("q1=gmegabuf(1/0);");assert(!pm_execute_runtime(&program,values,&line,&runtime));
    compile("q1=megabuf(1048576);");assert(!pm_execute_runtime(&program,values,&line,&runtime));
    /* Uniform defaults cover large logical ranges without allocating pages. */
    memset(&runtime,0,sizeof(runtime));
    compile("n=0;loop(300000,megabuf(n)=.1;n+=1;);q1=megabuf(299999);");pm_begin_frame();
    assert(pm_execute_init_runtime(&program,values,&line,&runtime));
    assert(values[PM_Q_BASE]==.1f && runtime.page_count==0 && runtime.fill_count==1);
    compile("megabuf(299999)=4;q1=megabuf(299998);q2=megabuf(299999);");
    assert(pm_execute_runtime(&program,values,&line,&runtime));
    assert(values[PM_Q_BASE]==.1f && values[PM_Q_BASE+1]==4);
    baseline=runtime;
    compile("n=0;loop(300000,megabuf(n)=2;n+=1;);megabuf(700000)=9;megabuf(-1)=0;");
    assert(!pm_execute_init_runtime(&program,values,&line,&runtime));
    assert(!memcmp(&baseline,&runtime,sizeof(runtime)));
    /* Other side effects cannot be removed by native fill recognition. */
    compile("n=0;loop(1000000,megabuf(n)=0;q1+=1;n+=1;);");pm_begin_frame();
    assert(!pm_execute_init_runtime(&program,values,&line,&runtime));
    /* Main work cannot consume the separate point/geometry allowance. */
    compile("q1=0;loop(1024,q1+=1;);");pm_begin_frame();
    assert(!pm_execute_runtime(&program,values,&line,&runtime));pm_begin_frame();
    assert(pm_execute_frame_runtime(&program,values,&line,&runtime));
    assert(values[PM_Q_BASE]==1024);
    compile("loop(1000000,q1+=1;);");pm_begin_frame();
    assert(!pm_execute_frame_runtime(&program,values,&line,&runtime));
    assert(pm_frame_remaining()==PM_TOTAL_FUEL);
    assert(!pm_execute_frame_runtime(&program,values,&line,&runtime));
    assert(pm_frame_remaining()==PM_TOTAL_FUEL);
    compile("q1=1;");assert(pm_execute_init_runtime(&program,values,&line,&runtime));
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
    compile("n=0;loop(64,gmegabuf(n)=2;n+=256;);megabuf(-1)=0;");
    assert(!pm_execute_frame_runtime(&program,values,&line,&runtime));
    compile("n=50000;loop(64,gmegabuf(n)=7;n+=256;);q1=gmegabuf(0);");
    assert(pm_execute_frame_runtime(&program,values,&line,&runtime));
    assert(values[PM_Q_BASE]==0);
    compile("gmegabuf(50000)=99;gmegabuf(0)=1;");
    assert(pm_execute_runtime(&program,values,&line,&runtime));
    assert(pm_resource_limits()&PM_LIMIT_GLOBAL_PAGES);
    compile("q1=gmegabuf(50000);q2=gmegabuf(0);");
    assert(pm_execute_runtime(&program,values,&line,&runtime));
    assert(values[PM_Q_BASE]==99 && values[PM_Q_BASE+1]==0);
    /* Reading and zeroing untouched addresses never consumes page capacity. */
    pm_reset_globals();pm_begin_frame();
    compile("n=0;loop(4096,q1=gmegabuf(n);gmegabuf(n)=0;n+=256;);");
    assert(pm_execute_frame_runtime(&program,values,&line,&runtime));
    compile("gmegabuf(1048575)=15;q1=gmegabuf(1048575);");
    assert(pm_execute_runtime(&program,values,&line,&runtime));
    assert(values[PM_Q_BASE]==15);
    /* Negative global indices wrap exactly like scalar GRAM access. */
    compile("gmegabuf(1048575)=15;gmegabuf(0)=9;gmegabuf(1)=11;n=-2;loop(3,gmegabuf(n)=0;n+=1;);"
            "q1=gmegabuf(1048575);q2=gmegabuf(0);q3=gmegabuf(1);");
    assert(pm_execute_init_runtime(&program,values,&line,&runtime));
    assert(values[PM_Q_BASE]==0 && values[PM_Q_BASE+1]==0 && values[PM_Q_BASE+2]==11);
    /* Local pages never alias on saturation; existing cells still work. */
    memset(&runtime,0,sizeof(runtime));pm_reset_globals();pm_begin_frame();
    compile("n=0;loop(32,megabuf(n)=7;n+=256;);megabuf(900000)=8;"
            "megabuf(0)=9;q1=megabuf(900000);q2=megabuf(0);");
    assert(pm_execute_frame_runtime(&program,values,&line,&runtime));
    assert(values[PM_Q_BASE]==0 && values[PM_Q_BASE+1]==9);
    assert(pm_resource_limits()&PM_LIMIT_LOCAL_PAGES);
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
    assert(diagnostic_seen==15);
    pm_diagnostic_hook=NULL;
    pm_program_free(&program);
    return 0;
}

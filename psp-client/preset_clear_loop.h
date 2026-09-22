/* SPDX-License-Identifier: GPL-2.0-or-later
 * Recognize constant local fills and global zero clears with index+=1.
 * No arbitrary branches or skipped side effects. Local ranges are clipped
 * to the logical PSP address space; shared GRAM uses Desktop's address mask.
 * Work is bounded by resident pages and uniform-range descriptors.
 */
static int clear_global_contains(int index,int lo,int hi) {
    /* Integer negative indices gain one from EEL's epsilon + truncation.
     * A range crossing zero therefore touches cell zero twice. */
    if(lo<0) {
        int end=hi<0?hi:0;
        if((((unsigned int)(index-lo-1))&(PM_GLOBAL_ADDRESS_SPACE-1))<(unsigned int)(end-lo))return 1;
    }
    if(hi>0) {
        int begin=lo>0?lo:0;
        if((((unsigned int)(index-begin))&(PM_GLOBAL_ADDRESS_SPACE-1))<(unsigned int)(hi-begin))return 1;
    }
    return 0;
}
static int clear_loop(const PmProgram *p,int start,float repeats,float *local,
    PmRuntime *runtime,Journal *journal,ValueJournal *variables,int *fuel,float *result) {
    int end=p->code[start].arg-1,at=start+1,mask=0,id=-1;
    float fill=0;
    while(at+3<end) {
        const PmOp *op=p->code+at;
        if(op[0].op!=LOAD || op[1].op!=PUSH || !isfinite(op[1].value) ||
           (op[2].op!=MEMS && op[2].op!=GMEMS) || op[2].value || op[3].op!=DROP)break;
        if(op[2].op==GMEMS && op[1].value!=0)return 0;
        if(op[2].op==MEMS)fill=assigned_value(op[1].value);
        if(id<0)id=op[0].arg;
        if(op[0].arg!=id)return 0;
        int bit=op[2].op==MEMS?1:2;
        if(mask&bit)return 0;
        mask|=bit;at+=4;
    }
    if(!mask || id<PM_USER_BASE || id>=PM_COORD_BASE || at+4!=end)return 0;
    const PmOp *op=p->code+at;
    if(op[0].op!=LOAD || op[0].arg!=id || op[1].op!=PUSH || op[1].value!=1 ||
       op[2].op!=ADD || op[3].op!=KEEP || op[3].arg!=id)return 0;
    float initial=local[id];
    if(!isfinite(initial) || initial!=floorf(initial) || fabsf(initial)>1048576)return 0;
    /* Desktop's loop limit, but native work stays bounded by resident memory.
     * All integer increments here are exactly representable as PSP floats. */
    int count=repeats<1?0:repeats>1048576?1048576:(int)repeats;
    if(!count){*result=0;return 1;}
    int lo=(int)initial,hi=lo+count,work=0;
    for(int type=0;type<2;type++)if(mask&(1<<type)) {
        if(type) {
            work+=global_page_count*PM_GLOBAL_PAGE_SIZE;
        } else {
            work+=runtime->page_count*PM_GLOBAL_PAGE_SIZE+PM_LOCAL_FILLS;
        }
    }
    if(work>*fuel || (frame_fuel>=0 && work>frame_fuel))return -1;
    *fuel-=work;if(frame_fuel>=0)frame_fuel-=work;
    if(pm_diagnostic_hook)pm_diagnostic_hook("VM native fill begin",p->code[start].line,count);
    for(int type=0;type<2;type++)if(mask&(1<<type)) {
        if(pm_diagnostic_hook)pm_diagnostic_hook(type?"VM shared clear begin":"VM local fill begin",p->code[start].line,count);
        if(type) {
            for(int page=0;page<global_page_count;page++) {
                int base=global_page_keys[page]*PM_GLOBAL_PAGE_SIZE;
                for(int i=0;i<PM_GLOBAL_PAGE_SIZE;i++)
                    if(clear_global_contains(base+i,lo,hi) &&
                       !write_value(journal,global_memory+page*PM_GLOBAL_PAGE_SIZE+i,0))return -1;
            }
        } else {
            if(!local_fill(runtime,lo,hi,fill,journal))return -1;
        }
        if(pm_diagnostic_hook)pm_diagnostic_hook(type?"VM shared clear end":"VM local fill end",p->code[start].line,count);
    }
    *result=(float)hi;variable_store(variables,local,id,*result);
    if(pm_diagnostic_hook)pm_diagnostic_hook("VM native fill end",p->code[start].line,count);
    return 1;
}

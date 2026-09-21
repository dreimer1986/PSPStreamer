/* SPDX-License-Identifier: GPL-2.0-or-later
 * Recognize ONLY loop(count, [g]megabuf(index)=0; ...; index=index+1).
 * No arbitrary loop truncation, nonzero fill, branches or skipped side effects.
 * Zeroing outside physically available PSP memory is intentionally clipped;
 * subsequent ordinary out-of-range accesses still fail explicitly. Sparse
 * global pages outside the cleared logical range must remain untouched.
 */
static int clear_loop(const PmProgram *p,int start,float repeats,float *local,
    PmRuntime *runtime,Journal *journal,ValueJournal *variables,int *fuel,float *result) {
    int end=p->code[start].arg-1,at=start+1,mask=0,id=-1;
    while(at+3<end) {
        const PmOp *op=p->code+at;
        if(op[0].op!=LOAD || op[1].op!=PUSH || op[1].value!=0 ||
           (op[2].op!=MEMS && op[2].op!=GMEMS) || op[2].value || op[3].op!=DROP)break;
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
            for(int page=0;page<global_page_count;page++) {
                int base=global_page_keys[page]*PM_GLOBAL_PAGE_SIZE;
                int first=lo>base?lo:base,last=hi<base+PM_GLOBAL_PAGE_SIZE?hi:base+PM_GLOBAL_PAGE_SIZE;
                if(last>first)work+=last-first;
            }
        } else {
            int first=lo<0?0:lo>PM_MEMORY?PM_MEMORY:lo,last=hi<0?0:hi>PM_MEMORY?PM_MEMORY:hi;
            work+=last-first;
        }
    }
    if(work>*fuel || (frame_fuel>=0 && work>frame_fuel))return -1;
    *fuel-=work;if(frame_fuel>=0)frame_fuel-=work;
    for(int type=0;type<2;type++)if(mask&(1<<type)) {
        if(type) {
            for(int page=0;page<global_page_count;page++) {
                int base=global_page_keys[page]*PM_GLOBAL_PAGE_SIZE;
                int first=lo>base?lo:base,last=hi<base+PM_GLOBAL_PAGE_SIZE?hi:base+PM_GLOBAL_PAGE_SIZE;
                for(int i=first;i<last;i++)
                    if(!write_value(journal,global_memory+page*PM_GLOBAL_PAGE_SIZE+i-base,0))return -1;
            }
        } else {
            int first=lo<0?0:lo>PM_MEMORY?PM_MEMORY:lo,last=hi<0?0:hi>PM_MEMORY?PM_MEMORY:hi;
            for(int i=first;i<last;i++)if(!write_value(journal,runtime->memory+i,0))return -1;
        }
    }
    *result=(float)hi;variable_store(variables,local,id,*result);
    return 1;
}

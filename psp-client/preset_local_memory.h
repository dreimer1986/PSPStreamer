/* Sparse local megabuf with bounded resident pages. Uniform initialization
 * ranges are stored as defaults, not hundreds of thousands of duplicate cells.
 * Runtime and rollback contain no heap pointers; context copies stay valid. */
static float local_default(const PmRuntime *r,int index) {
    for(int i=r->fill_count-1;i>=0;i--)
        if(index>=r->fills[i].lo && index<r->fills[i].hi)return r->fills[i].value;
    return 0;
}
static float *local_cell(PmRuntime *r,int index,int create) {
    int page=index/PM_GLOBAL_PAGE_SIZE,physical=r->pages[page];
    if(!physical && create) {
        if(r->page_count>=PM_MEMORY/PM_GLOBAL_PAGE_SIZE){resource_limits|=PM_LIMIT_LOCAL_PAGES;return NULL;}
        physical=++r->page_count;r->keys[physical-1]=(unsigned short)page;
        r->pages[page]=(unsigned char)physical;
        for(int i=0;i<PM_GLOBAL_PAGE_SIZE;i++)
            r->memory[(physical-1)*PM_GLOBAL_PAGE_SIZE+i]=local_default(r,page*PM_GLOBAL_PAGE_SIZE+i);
    }
    return physical?r->memory+(physical-1)*PM_GLOBAL_PAGE_SIZE+index%PM_GLOBAL_PAGE_SIZE:NULL;
}
static float local_read(PmRuntime *r,int index) {
    float *cell=local_cell(r,index,0);return cell?*cell:local_default(r,index);
}
static int local_write(PmRuntime *r,int index,float value,Journal *journal) {
    float *cell=local_cell(r,index,0);
    if(!cell && value==local_default(r,index) && !signbit(value))return 1;
    if(!cell)cell=local_cell(r,index,1);
    /* Never alias unrelated logical addresses or evict live data. Beyond
     * the PSP resident budget, absent cells retain their uniform default. */
    return !cell || write_value(journal,cell,value);
}
static int local_fill(PmRuntime *r,int lo,int hi,float value,Journal *journal) {
    if(lo<0)lo=0;
    if(hi>PM_GLOBAL_ADDRESS_SPACE)hi=PM_GLOBAL_ADDRESS_SPACE;
    if(lo>=hi)return 1;
    /* Drop fully overwritten ranges; a zero fill needs no default unless it
     * covers only part of an older nonzero default. */
    int used=0;
    for(int i=0;i<r->fill_count;i++)
        if(r->fills[i].lo<lo || r->fills[i].hi>hi)r->fills[used++]=r->fills[i];
    r->fill_count=used;
    if(value!=0 || signbit(value) || used) {
        if(used>=PM_LOCAL_FILLS){PM_REASON("local fill capacity");return 0;}
        r->fills[r->fill_count++]=(PmFill){lo,hi,value};
    }
    for(int p=0;p<r->page_count;p++) {
        int base=r->keys[p]*PM_GLOBAL_PAGE_SIZE;
        int first=lo>base?lo:base,last=hi<base+PM_GLOBAL_PAGE_SIZE?hi:base+PM_GLOBAL_PAGE_SIZE;
        for(int i=first;i<last;i++)
            if(!write_value(journal,r->memory+p*PM_GLOBAL_PAGE_SIZE+i-base,value))return 0;
    }
    return 1;
}

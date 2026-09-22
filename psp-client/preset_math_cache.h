/* Exact memoization of pure, expensive scalar operations, renderer-thread
 * owned. Keys are opcode + current operand bits, not variable identities:
 * assignments, point changes, preset changes and signed zero cannot produce
 * stale hits. No random, memory, register, assignment or control operations.
 * This extends the existing sin/cos cache, not an approximation table. */
enum { PM_MATH_CACHE_SIZE=64 };
typedef struct {uint32_t a,b;int op,valid;float result;} PmMathCacheEntry;
static PmMathCacheEntry pm_math_cache[PM_MATH_CACHE_SIZE];
static void pm_math_cache_reset(void) {memset(pm_math_cache,0,sizeof(pm_math_cache));}
static int pm_math_cache_lookup(int op,float a,float b,float *result,int *slot) {
    *slot=-1;
#ifdef PM_REFERENCE_EXECUTION
    (void)op;(void)a;(void)b;(void)result;return 0;
#else
    switch(op) {
    case POW:case ATAN2:case ATAN:case EXP:case LOG:case LOG10:
    case TAN:case ASIN:case ACOS:break;
    default:return 0;
    }
    if(!isfinite(a) || !isfinite(b))return 0;
    uint32_t aa,bb;memcpy(&aa,&a,4);memcpy(&bb,&b,4);
    uint32_t hash=aa^(aa>>16)^bb^(bb>>13)^((uint32_t)op*0x9e3779b9U);
    int index=(int)(hash&(PM_MATH_CACHE_SIZE-1));
    PmMathCacheEntry *entry=&pm_math_cache[index];
    if(entry->valid && entry->op==op && entry->a==aa && entry->b==bb) {
        *result=entry->result;return 1;
    }
    entry->op=op;entry->a=aa;entry->b=bb;entry->valid=0;*slot=index;
    return 0;
#endif
}
static void pm_math_cache_store(int slot,float result) {
    if(slot>=0 && isfinite(result)) {
        pm_math_cache[slot].result=result;pm_math_cache[slot].valid=1;
    }
}

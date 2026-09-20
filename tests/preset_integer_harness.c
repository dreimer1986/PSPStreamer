/* Compare the bounded PSP conversion helpers to the reference's actual
 * x87 FISTP operations, with NS-EEL's round-toward-zero control word. */
#include <assert.h>
#include "preset_math.c"
#if defined(__i386__) || defined(__x86_64__)
static int64_t reference_bits(float value) {
    int64_t result;
    __asm__ volatile("flds %1; fistpll %0" : "=m"(result) : "m"(value) : "st");
    return result;
}
static uint32_t reference_mod(float value) {
    uint32_t result;
    __asm__ volatile("flds %1; fabs; fistpl %0" : "=m"(result) : "m"(value) : "st");
    return result;
}
#endif
int main(void) {
    assert(eel_bit_integer(INFINITY)==INT64_MIN);
    assert(eel_bit_integer(NAN)==INT64_MIN);
    assert(eel_mod_integer(NAN)==0x80000000U);
#if defined(__i386__) || defined(__x86_64__)
    unsigned short original,chop;
    __asm__ volatile("fnstcw %0" : "=m"(original));
    chop=(original|0x0c3f);
    __asm__ volatile("fldcw %0" : : "m"(chop));
    uint32_t bits=1234567;
    for(int i=0;i<100000;i++) {
        bits=bits*1664525U+1013904223U;
        float value;memcpy(&value,&bits,sizeof(value));
        assert(eel_bit_integer(value)==reference_bits(value));
        assert(eel_mod_integer(value)==reference_mod(value));
    }
    const float edges[]={0,-0.0f,1.9f,-1.9f,INFINITY,-INFINITY,NAN,
        0x1p31f,-0x1p31f,0x1.fffffep30f,0x1p63f,-0x1p63f,0x1.fffffep62f};
    for(unsigned i=0;i<sizeof(edges)/sizeof(*edges);i++) {
        assert(eel_bit_integer(edges[i])==reference_bits(edges[i]));
        assert(eel_mod_integer(edges[i])==reference_mod(edges[i]));
    }
    __asm__ volatile("fnclex; fldcw %0" : : "m"(original));
#endif
    return 0;
}
